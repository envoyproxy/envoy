#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/trace/v3/datadog.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

constexpr uint32_t kPrimaryUpstream = 0;
constexpr uint32_t kSideUpstream = 1;

// cluster_0 receives the request, "side" receives a traced request that outlives the downstream
// request, and "datadog_agent" is the Datadog tracer's collector cluster. Each cluster has one
// endpoint, so the clusters map to fake_upstreams_[0], [1] and [2] in order.
//
// On worker shutdown the thread local tracer is destroyed before the thread local cluster manager,
// so the span of the side request holds the last reference to dd-trace-cpp's DatadogAgent.
// Destroying the thread local cluster manager resets the side request, which finishes the span,
// which destroys the DatadogAgent, whose destructor flushes the trace while the thread local
// cluster manager is being destroyed.
class DatadogTracerIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  DatadogTracerIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setUpstreamCount(3);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      for (const char* name : {"side", "datadog_agent"}) {
        auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
        cluster->MergeFrom(bootstrap.static_resources().clusters(0));
        cluster->set_name(name);
        cluster->mutable_load_assignment()->set_cluster_name(name);
      }
    });

    config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
      envoy::config::trace::v3::DatadogConfig datadog_config;
      datadog_config.set_collector_cluster("datadog_agent");
      datadog_config.set_service_name("envoy-integration-test");
      auto* provider = hcm.mutable_tracing()->mutable_provider();
      provider->set_name("envoy.tracers.datadog");
      ASSERT_TRUE(provider->mutable_typed_config()->PackFrom(datadog_config));
    });
  }

  // Mirror every request to the side cluster, as request_mirror_policies do in production. A
  // mirror request is traced and outlives its downstream request.
  void addMirrorPolicy() {
    config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      route->mutable_route()->add_request_mirror_policies()->set_cluster("side");
    });
  }

  // Make an asynchronous Lua httpCall() to the side cluster. It is traced as a child of the
  // downstream span, and it is not cancelled when the downstream stream is destroyed.
  void addLuaAsynchronousCall() {
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.lua
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
  default_source_code:
    inline_string: |
      function envoy_on_request(request_handle)
        request_handle:httpCall(
          "side",
          {[":method"] = "GET", [":path"] = "/side", [":authority"] = "side"},
          "",
          60000,
          true)
      end
)EOF");
  }

  // Give the side cluster a cluster-level retry policy that retries resets. The router applies a
  // cluster-level policy to mirror requests too, so a mirror request whose connection pool is
  // destroyed waits for a retry instead of completing. It is reset again when its cluster entry is
  // destroyed.
  void addSideClusterRetryPolicy() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* side = bootstrap.mutable_static_resources()->mutable_clusters(1);
      ASSERT_EQ("side", side->name());
      auto& options_any = (*side->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
      envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
      if (options_any.type_url().empty()) {
        options.mutable_explicit_http_config()->mutable_http2_protocol_options();
      } else {
        ASSERT_TRUE(options_any.UnpackTo(&options));
      }
      auto* retry_policy = options.mutable_retry_policy();
      retry_policy->set_retry_on("reset");
      retry_policy->mutable_num_retries()->set_value(3);
      retry_policy->mutable_retry_back_off()->mutable_base_interval()->set_seconds(10);
      ASSERT_TRUE(options_any.PackFrom(options));
    });
  }

  // Send a request, complete it, and shut the server down while the side request is still in
  // flight. Success is that nothing crashes or asserts.
  void shutdownWithInFlightSideRequest() {
    initialize();

    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);

    // Leave the side request in flight: it is never answered.
    FakeHttpConnectionPtr side_connection;
    ASSERT_TRUE(
        fake_upstreams_[kSideUpstream]->waitForHttpConnection(*dispatcher_, side_connection));
    FakeStreamPtr side_request;
    ASSERT_TRUE(side_connection->waitForNewStream(*dispatcher_, side_request));
    ASSERT_TRUE(side_request->waitForEndStream(*dispatcher_));

    waitForNextUpstreamRequest(kPrimaryUpstream);
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    codec_client_->close();

    test_server_.reset();
    ASSERT_TRUE(side_connection->waitForDisconnect());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    fake_upstream_connection_.reset();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DatadogTracerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DatadogTracerIntegrationTest, ShutdownWithInFlightTracedMirrorRequest) {
  addMirrorPolicy();
  shutdownWithInFlightSideRequest();
}

TEST_P(DatadogTracerIntegrationTest, ShutdownWithInFlightTracedLuaRequest) {
  addLuaAsynchronousCall();
  shutdownWithInFlightSideRequest();
}

// Same as the mirror test, except that the mirror request is waiting for a retry when its
// connection pool is destroyed, so it is reset later, while the thread local cluster entries are
// destroyed. Its span then flushes the trace after the entry of the collector cluster may already
// be gone, so the tracer must not use a cluster it looked up earlier. The entries are destroyed in
// hash map order, which depends on per-process and per-table hash seeds, so only about half of the
// runs destroy the collector cluster first: this test is best-effort for that order.
TEST_P(DatadogTracerIntegrationTest, ShutdownWithTracedMirrorRequestAwaitingRetry) {
  addMirrorPolicy();
  addSideClusterRetryPolicy();
  shutdownWithInFlightSideRequest();
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
