#include <chrono>
#include <string>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"
#include "test/integration/socket_interface_swap.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

class ShadowPolicyIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest,
      public SocketInterfaceSwap {
public:
  ShadowPolicyIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, std::get<0>(GetParam())),
        SocketInterfaceSwap(Network::Socket::Type::Stream) {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.streaming_shadow", streaming_shadow_ ? "true" : "false"}});
    setUpstreamProtocol(Http::CodecType::HTTP2);
    autonomous_upstream_ = true;
    setUpstreamCount(2);
  }

  // Adds a mirror policy that routes to cluster_header or cluster_name, in that order. Additionally
  // optionally registers an upstream HTTP filter on the cluster specified by
  // cluster_with_custom_filter_.
  void initialConfigSetup(const std::string& cluster_name, const std::string& cluster_header) {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name(std::string(Envoy::RepickClusterFilter::ClusterName));
      ConfigHelper::setHttp2(*cluster);
      if (cluster_with_custom_filter_.has_value()) {
        auto* cluster =
            bootstrap.mutable_static_resources()->mutable_clusters(*cluster_with_custom_filter_);

        ConfigHelper::HttpProtocolOptions protocol_options =
            MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
                (*cluster->mutable_typed_extension_protocol_options())
                    ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
        protocol_options.add_http_filters()->set_name(filter_name_);
        auto* upstream_codec = protocol_options.add_http_filters();
        upstream_codec->set_name("envoy.filters.http.upstream_codec");
        upstream_codec->mutable_typed_config()->PackFrom(
            envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::
                default_instance());
        (*cluster->mutable_typed_extension_protocol_options())
            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                .PackFrom(protocol_options);
      }
    });

    // Set the mirror policy with cluster header or cluster name.
    config_helper_.addConfigModifier(
        [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* mirror_policy = hcm.mutable_route_config()
                                    ->mutable_virtual_hosts(0)
                                    ->mutable_routes(0)
                                    ->mutable_route()
                                    ->add_request_mirror_policies();
          if (!cluster_header.empty()) {
            mirror_policy->set_cluster_header(cluster_header);
          } else {
            mirror_policy->set_cluster(cluster_name);
          }
        });
  }

  void sendRequestAndValidateResponse(int times_called = 1) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    if (filter_name_ != "add-body-filter") {
      EXPECT_EQ(10U, response->body().size());
    }
    test_server_->waitForCounterGe("cluster.cluster_1.internal.upstream_rq_completed",
                                   times_called);

    upstream_headers_ =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
    EXPECT_TRUE(upstream_headers_ != nullptr);
    mirror_headers_ =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[1].get())->lastRequestHeaders();
    EXPECT_TRUE(mirror_headers_ != nullptr);

    cleanupUpstreamAndDownstream();
  }

  const bool streaming_shadow_ = std::get<1>(GetParam());
  absl::optional<int> cluster_with_custom_filter_;
  std::string filter_name_ = "on-local-reply-filter";
  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers_;
  std::unique_ptr<Http::TestRequestHeaderMapImpl> mirror_headers_;
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsAndStreaming, ShadowPolicyIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    [](const ::testing::TestParamInfo<ShadowPolicyIntegrationTest::ParamType>& params) {
      return absl::StrCat(std::get<0>(params.param) == Network::Address::IpVersion::v4 ? "IPv4"
                                                                                       : "IPv6",
                          "_", std::get<1>(params.param) ? "streaming_shadow" : "buffered_shadow");
    });

TEST_P(ShadowPolicyIntegrationTest, Basic) {
  initialConfigSetup("cluster_1", "");
  initialize();

  sendRequestAndValidateResponse(1);
  sendRequestAndValidateResponse(2);

  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_200", 2);
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_cx_total")->value());
}

TEST_P(ShadowPolicyIntegrationTest, BasicWithLimits) {
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->set_connection_pool_per_downstream_connection(true);
    bootstrap.mutable_static_resources()
        ->mutable_clusters(1)
        ->set_connection_pool_per_downstream_connection(true);
  });
  initialize();

  sendRequestAndValidateResponse(1);
  sendRequestAndValidateResponse(2);

  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_200", 2);
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  // https://github.com/envoyproxy/envoy/issues/26820
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_cx_total")->value());
}

TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithDownstreamReset) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  initialConfigSetup("cluster_1", "");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, false);
  auto& encoder = result.first;
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_main->headers().get(Http::LowerCaseString("potato"))[0]->value(),
            "salad");
  EXPECT_EQ(upstream_request_shadow->headers().get(Http::LowerCaseString("potato"))[0]->value(),
            "salad");

  codec_client_->sendReset(encoder);

  ASSERT_TRUE(upstream_request_main->waitForReset());
  ASSERT_TRUE(upstream_request_shadow->waitForReset());
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());

  EXPECT_FALSE(upstream_request_main->complete());
  EXPECT_FALSE(upstream_request_shadow->complete());
  EXPECT_FALSE(response->complete());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 0);
}

TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithMainUpstreamReset) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  initialConfigSetup("cluster_1", "");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, false);
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());

  // Send upstream reset on main request.
  upstream_request_main->encodeResetStream();
  ASSERT_TRUE(response->waitForReset());
  ASSERT_TRUE(upstream_request_shadow->waitForReset());

  ASSERT_TRUE(upstream_request_shadow->waitForReset());
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());

  EXPECT_FALSE(upstream_request_main->complete());
  EXPECT_FALSE(upstream_request_shadow->complete());
  EXPECT_TRUE(response->complete());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  // Main cluster saw remote reset; shadow cluster saw local reset.
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_rx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 0);
}

TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithShadowUpstreamReset) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  initialConfigSetup("cluster_1", "");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, false);
  auto& encoder = result.first;
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());

  // Send upstream reset on shadow request.
  upstream_request_shadow->encodeResetStream();
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_rx_reset", 1,
                                 std::chrono::milliseconds(1000));

  codec_client_->sendData(encoder, 20, true);
  ASSERT_TRUE(upstream_request_main->waitForData(*dispatcher_, 20));
  ASSERT_TRUE(upstream_request_main->waitForEndStream(*dispatcher_));
  upstream_request_main->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());

  EXPECT_TRUE(upstream_request_main->complete());
  EXPECT_FALSE(upstream_request_shadow->complete());
  EXPECT_TRUE(response->complete());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  // Main cluster saw no reset; shadow cluster saw remote reset.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_rx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 1);
}

// Test a downstream timeout before end stream has been sent.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithEarlyDownstreamTimeout) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        // 100 millisecond timeout.
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        hcm.mutable_stream_idle_timeout()->set_nanos(100 * 1000 * 1000);
      });
  initialConfigSetup("cluster_1", "");
  config_helper_.disableDelayClose();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, false);
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());

  // Eventually the request will time out.
  ASSERT_TRUE(response->waitForReset());
  ASSERT_TRUE(upstream_request_main->waitForReset());
  ASSERT_TRUE(upstream_request_shadow->waitForReset());

  // Clean up.
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());

  EXPECT_FALSE(upstream_request_main->complete());
  EXPECT_FALSE(upstream_request_shadow->complete());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 0);
}

// Test a downstream timeout after end stream has been sent by the client where the shadow request
// completes.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithLateDownstreamTimeoutAndShadowComplete) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        // 100 millisecond timeout.
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        hcm.mutable_stream_idle_timeout()->set_nanos(100 * 1000 * 1000);
      });
  config_helper_.disableDelayClose();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  // Send whole request.
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, true);
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());
  upstream_request_shadow->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());
  EXPECT_TRUE(upstream_request_shadow->complete());

  // Eventually the main request will time out.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(upstream_request_main->waitForReset());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "504");

  // Clean up.
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_tx_reset")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 1);
}

// Test a shadow timeout after the main request completes.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithShadowOnlyTimeout) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        // 100 millisecond timeout. The shadow inherits the timeout value from the route.
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->mutable_routes(0)->mutable_route();
        route->mutable_timeout()->set_seconds(0);
        route->mutable_timeout()->set_nanos(100 * 1000 * 1000);
      });
  config_helper_.disableDelayClose();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  // Send whole request.
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, true);
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());

  upstream_request_main->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Eventually the shadow request will time out.
  ASSERT_TRUE(upstream_request_shadow->waitForReset());

  // Clean up.
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_tx_reset")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_timeout")->value(), 1);
}

TEST_P(ShadowPolicyIntegrationTest, MainRequestOverBufferLimit) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = true;
  if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
    // With deferred processing, a local reply is triggered so the upstream
    // stream will be incomplete.
    autonomous_allow_incomplete_streams_ = true;
  }
  cluster_with_custom_filter_ = 0;
  filter_name_ = "encoder-decoder-buffer-filter";
  initialConfigSetup("cluster_1", "");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.disableDelayClose();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  // Send whole (large) request.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
    // With deferred processing, the encoder-decoder-buffer-filter will
    // buffer too much data triggering a local reply.
    test_server_->waitForCounterEq("http.config_test.downstream_rq_4xx", 1);
  } else {
    test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_completed", 1);
  }
}

TEST_P(ShadowPolicyIntegrationTest, ShadowRequestOverBufferLimit) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = true;
  cluster_with_custom_filter_ = 1;
  filter_name_ = "encoder-decoder-buffer-filter";
  initialConfigSetup("cluster_1", "");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.disableDelayClose();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  // Send whole (large) request.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 1);
  // The request to the shadow upstream never completed due to buffer overflow.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 0);
}

TEST_P(ShadowPolicyIntegrationTest, ShadowRequestOverRouteBufferLimit) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = true;
  cluster_with_custom_filter_ = 1;
  filter_name_ = "encoder-decoder-buffer-filter";
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
    hcm.mutable_route_config()
        ->mutable_virtual_hosts(0)
        ->mutable_per_request_buffer_limit_bytes()
        ->set_value(0);
  });
  config_helper_.disableDelayClose();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  // Send whole (large) request.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->headers().getStatusValue(), "200");

  cleanupUpstreamAndDownstream();

  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_rq_completed")->value(), 1);
  // The request to the shadow upstream never completed due to buffer overflow.
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_rq_completed")->value(), 0);
}

TEST_P(ShadowPolicyIntegrationTest, BackedUpConnectionBeforeShadowBegins) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialConfigSetup("cluster_1", "");
  // Shrink the shadow buffer size.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bs) {
    auto* shadow_cluster = bs.mutable_static_resources()->mutable_clusters(1);
    shadow_cluster->mutable_per_connection_buffer_limit_bytes()->set_value(1024);
  });

  // Add a route directly for the shadow.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route_1 = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
        route_1->mutable_route()->set_cluster("cluster_1");
        route_1->mutable_match()->set_prefix("/shadow");
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_routes(0)
            ->mutable_match()
            ->set_prefix("/main");
      });
  config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams, "true");
  initialize();

  write_matcher_->setDestinationPort(fake_upstreams_[1]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send an initial request to `cluster_1` directly, which will fill the connection buffer.
  auto shadow_direct_response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shadow"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 3);

  // Initiate a request to the main route.
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/main"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                  false);
  auto& encoder = result.first;
  auto main_response = std::move(result.second);

  // Connecting to the shadow stream should cause backpressure due to connection backup.
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total", 1,
                                 std::chrono::milliseconds(500));

  codec_client_->sendData(encoder, 1023, false);

  codec_client_->sendData(encoder, 10, true);
  // Main request must not have been completed due to backpressure.
  EXPECT_FALSE(main_response->waitForEndStream(std::chrono::milliseconds(500)));

  // Unblock the port for `cluster_1`
  write_matcher_->setResumeWrites();

  EXPECT_TRUE(main_response->waitForEndStream());
  EXPECT_TRUE(main_response->complete());
  EXPECT_EQ(main_response->headers().getStatusValue(), "200");
  EXPECT_TRUE(shadow_direct_response->waitForEndStream());
  EXPECT_TRUE(shadow_direct_response->complete());
  EXPECT_EQ(shadow_direct_response->headers().getStatusValue(), "200");

  // Two requests were sent over a single connection to cluster_1.
  test_server_->waitForCounterGe("cluster.cluster_1.upstream_rq_completed", 2);
  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_flow_control_paused_reading_total")
                ->value(),
            1);
}

TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithShadowBackpressure) {
  if (!streaming_shadow_) {
    GTEST_SKIP() << "Not applicable for non-streaming shadows.";
  }
  autonomous_upstream_ = false;
  initialConfigSetup("cluster_1", "");
  // Shrink the shadow buffer size.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bs) {
    auto* shadow_cluster = bs.mutable_static_resources()->mutable_clusters(1);
    shadow_cluster->mutable_per_connection_buffer_limit_bytes()->set_value(1024);
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("potato", "salad");
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> result =
      codec_client_->startRequest(request_headers, false);
  auto& encoder = result.first;
  auto response = std::move(result.second);

  FakeHttpConnectionPtr fake_upstream_connection_main;
  FakeStreamPtr upstream_request_main;
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_main));
  ASSERT_TRUE(fake_upstream_connection_main->waitForNewStream(*dispatcher_, upstream_request_main));
  FakeHttpConnectionPtr fake_upstream_connection_shadow;
  FakeStreamPtr upstream_request_shadow;
  ASSERT_TRUE(
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_shadow));
  ASSERT_TRUE(
      fake_upstream_connection_shadow->waitForNewStream(*dispatcher_, upstream_request_shadow));
  ASSERT_TRUE(upstream_request_main->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_shadow->waitForHeadersComplete());

  // This will result in one call of high watermark on the shadow stream, as
  // end_stream will not trigger watermark calls.
  codec_client_->sendData(encoder, 2048, false);
  test_server_->waitForCounterGe("http.config_test.downstream_flow_control_paused_reading_total",
                                 1);
  codec_client_->sendData(encoder, 2048, true);
  ASSERT_TRUE(upstream_request_main->waitForData(*dispatcher_, 2048 * 2));
  ASSERT_TRUE(upstream_request_shadow->waitForData(*dispatcher_, 2048 * 2));

  ASSERT_TRUE(upstream_request_main->waitForEndStream(*dispatcher_));
  ASSERT_TRUE(upstream_request_shadow->waitForEndStream(*dispatcher_));
  upstream_request_main->encodeHeaders(default_response_headers_, true);
  upstream_request_shadow->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(fake_upstream_connection_main->close());
  ASSERT_TRUE(fake_upstream_connection_shadow->close());
  ASSERT_TRUE(fake_upstream_connection_main->waitForDisconnect());
  ASSERT_TRUE(fake_upstream_connection_shadow->waitForDisconnect());
  EXPECT_TRUE(upstream_request_main->complete());
  EXPECT_TRUE(upstream_request_shadow->complete());
  EXPECT_TRUE(response->complete());

  cleanupUpstreamAndDownstream();

  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total",
                                 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_cx_total", 1);
  // Main cluster saw no reset; shadow cluster saw remote reset.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_completed", 1);
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_completed", 1);
}

// Test request mirroring / shadowing with the cluster name in policy.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithCluster) {
  initialConfigSetup("cluster_1", "");
  initialize();

  sendRequestAndValidateResponse();

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
}

// Test request mirroring / shadowing with upstream HTTP filters in the router.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithRouterUpstreamFilters) {
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) -> void {
    auto* router_filter_config = hcm.mutable_http_filters(hcm.http_filters_size() - 1);
    envoy::extensions::filters::http::router::v3::Router router_filter;
    router_filter_config->typed_config().UnpackTo(&router_filter);
    router_filter.add_upstream_http_filters()->set_name("add-body-filter");
    auto* upstream_codec = router_filter.add_upstream_http_filters();
    upstream_codec->set_name("envoy.filters.http.upstream_codec");
    upstream_codec->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::default_instance());
    router_filter_config->mutable_typed_config()->PackFrom(router_filter);
  });
  filter_name_ = "add-body-filter";
  initialize();
  sendRequestAndValidateResponse();

  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "4");
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "4");
}

// Test that a cluster-specified filter will override router-specified filters.
TEST_P(ShadowPolicyIntegrationTest, ClusterFilterOverridesRouterFilter) {
  initialConfigSetup("cluster_1", "");
  // main cluster adds body:
  cluster_with_custom_filter_ = 0;
  filter_name_ = "add-body-filter";

  // router filter upstream HTTP filter adds header:
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        auto* router_filter_config = hcm.mutable_http_filters(hcm.http_filters_size() - 1);
        envoy::extensions::filters::http::router::v3::Router router_filter;
        router_filter_config->typed_config().UnpackTo(&router_filter);
        router_filter.add_upstream_http_filters()->set_name("add-header-filter");
        router_filter.add_upstream_http_filters()->set_name("envoy.filters.http.upstream_codec");
        router_filter_config->mutable_typed_config()->PackFrom(router_filter);
      });

  initialize();
  sendRequestAndValidateResponse();
  // cluster_0 (main cluster) hits AddBodyFilter
  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "4");
  EXPECT_TRUE(upstream_headers_->get(Http::LowerCaseString("x-header-to-add")).empty());
  // cluster_1 (shadow_cluster) hits AddHeaderFilter.
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "");
  EXPECT_FALSE(mirror_headers_->get(Http::LowerCaseString("x-header-to-add")).empty());
}

// Test request mirroring / shadowing with the cluster header.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithClusterHeaderWithFilter) {
  initialConfigSetup("", "cluster_header_1");

  // Add a filter to set cluster_header in headers.
  config_helper_.addFilter("name: repick-cluster-filter");

  initialize();
  sendRequestAndValidateResponse();
}

// Test request mirroring / shadowing with the original cluster having a local reply filter.
TEST_P(ShadowPolicyIntegrationTest, OriginalClusterWithLocalReply) {
  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 0;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test request mirroring / shadowing with the mirror cluster having a local reply filter.
TEST_P(ShadowPolicyIntegrationTest, MirrorClusterWithLocalReply) {
  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 1;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ShadowPolicyIntegrationTest, OriginalClusterWithAddBody) {
  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 0;
  filter_name_ = "add-body-filter";

  initialize();
  sendRequestAndValidateResponse();
  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "4");
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "");
}

TEST_P(ShadowPolicyIntegrationTest, MirrorClusterWithAddBody) {
  auto log_file = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* typed_config =
            hcm.mutable_http_filters(hcm.http_filters_size() - 1)->mutable_typed_config();

        envoy::extensions::filters::http::router::v3::Router router_config;
        auto* upstream_log_config = router_config.add_upstream_log();
        upstream_log_config->set_name("accesslog");
        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        access_log_config.set_path(log_file);
        access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
            "%REQ(CONTENT-LENGTH)%\n");
        upstream_log_config->mutable_typed_config()->PackFrom(access_log_config);
        typed_config->PackFrom(router_config);
      });

  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 1;
  filter_name_ = "add-body-filter";

  initialize();
  sendRequestAndValidateResponse();
  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "");
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "4");

  std::string log1 = waitForAccessLog(log_file, 0, true);
  std::string log2 = waitForAccessLog(log_file, 1);
  EXPECT_TRUE((log1 == "4" && log2 == "-") || (log1 == "-" && log2 == "4"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("http.async-client.rq_total")->value());
}

TEST_P(ShadowPolicyIntegrationTest, ShadowedClusterHostHeaderAppendsSuffix) {
  initialConfigSetup("cluster_1", "");
  // By default `disable_shadow_host_suffix_append` is "false".
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_cluster("cluster_1");
      });

  initialize();
  sendRequestAndValidateResponse();
  // Ensure shadowed host header has suffix "-shadow".
  EXPECT_EQ(upstream_headers_->Host()->value().getStringView(), "sni.lyft.com");
  EXPECT_EQ(mirror_headers_->Host()->value().getStringView(), "sni.lyft.com-shadow");
}

TEST_P(ShadowPolicyIntegrationTest, ShadowedClusterHostHeaderAppendsSuffixAddresses) {
  initialConfigSetup("cluster_1", "");
  // By default `disable_shadow_host_suffix_append` is "false".
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_cluster("cluster_1");
      });

  initialize();
  default_request_headers_.setHost(fake_upstreams_[0]->localAddress()->asStringView());
  sendRequestAndValidateResponse();
  // Ensure shadowed host header has suffix "-shadow".
  EXPECT_EQ(upstream_headers_->getHostValue(), fake_upstreams_[0]->localAddress()->asStringView());
  EXPECT_EQ(mirror_headers_->getHostValue(),
            absl::StrCat(version_ == Network::Address::IpVersion::v4 ? "127.0.0.1" : "[::1]",
                         "-shadow:", fake_upstreams_[0]->localAddress()->ip()->port()))
      << mirror_headers_->getHostValue();
}

TEST_P(ShadowPolicyIntegrationTest, ShadowedClusterHostHeaderDisabledAppendSuffix) {
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_disable_shadow_host_suffix_append(true);
        mirror_policy->set_cluster("cluster_1");
      });

  initialize();
  sendRequestAndValidateResponse();
  // Ensure shadowed host header does not have suffix "-shadow".
  EXPECT_EQ(upstream_headers_->Host()->value().getStringView(), "sni.lyft.com");
  EXPECT_EQ(mirror_headers_->Host()->value().getStringView(), "sni.lyft.com");
}

} // namespace
} // namespace Envoy
