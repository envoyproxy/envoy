#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace {

#ifdef ENVOY_ENABLE_QUIC

class FilterIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void initialize() override {
    const std::string filename = TestEnvironment::temporaryPath("alt_svc_cache.txt");
    envoy::config::core::v3::AlternateProtocolsCacheOptions alt_cache;
    alt_cache.set_name("default_alternate_protocols_cache");
    envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig config;
    config.set_filename(filename);
    envoy::config::common::key_value::v3::KeyValueStoreConfig kv_config;
    kv_config.mutable_config()->set_name("envoy.key_value.file_based");
    kv_config.mutable_config()->mutable_typed_config()->PackFrom(config);
    alt_cache.mutable_key_value_store_config()->set_name("envoy.common.key_value");
    alt_cache.mutable_key_value_store_config()->mutable_typed_config()->PackFrom(kv_config);

    const std::string filter = fmt::format(R"EOF(
name: alternate_protocols_cache
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig
  alternate_protocols_cache_options:
    name: default_alternate_protocols_cache
    key_value_store_config:
      name: "envoy.common.key_value"
      typed_config:
        "@type": type.googleapis.com/envoy.config.common.key_value.v3.KeyValueStoreConfig
        config:
          name: envoy.key_value.file_based
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
            filename: {}

)EOF",
                                           filename);
    config_helper_.prependFilter(filter);

    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(/*use_alpn=*/true, /*http3=*/true, alt_cache);

    HttpProtocolIntegrationTest::initialize();
  }

  void createUpstreams() override {
    // The integration test infrastructure does not know how to create mixed protocol upstreams.
    // It will have created an HTTP/3 upstream, so the test needs to create an additional
    // HTTP/2 upstream manually.
    ASSERT_EQ(upstreamProtocol(), Http::CodecType::HTTP3);
    ASSERT_EQ(fake_upstreams_count_, 1);
    ASSERT_FALSE(autonomous_upstream_);

    auto config = configWithType(Http::CodecType::HTTP2);
    Network::TransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
    addFakeUpstream(std::move(factory), Http::CodecType::HTTP2);
  }
};

TEST_P(FilterIntegrationTest, AltSvc) {
  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  int port = fake_upstreams_[0]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/2. The response includes an Alt-Svc header.
  auto response = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 1);

  // Second request should go out over HTTP/3 because of the Alt-Svc information.
  auto response2 = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                 response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response2.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 1);
}

INSTANTIATE_TEST_SUITE_P(Protocols, FilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

class MixedUpstreamIntegrationTest : public FilterIntegrationTest {
protected:
  void writeFile() {
    const std::string filename = TestEnvironment::temporaryPath("alt_svc_cache.txt");
    // There's no hostname here because we're not doing dynamic forward proxying so we infer the
    // hostname from the config (which does not set it)
    uint32_t port = fake_upstreams_[0]->localAddress()->ip()->port();
    std::string key = absl::StrCat("https://:", port);

    size_t seconds = std::chrono::duration_cast<std::chrono::seconds>(
                         timeSystem().monotonicTime().time_since_epoch())
                         .count();
    std::string value = absl::StrCat("h3=\":", port, "\"; ma=", 86400 + seconds);
    TestEnvironment::writeStringToFileForTest(
        "alt_svc_cache.txt", absl::StrCat(key.length(), "\n", key, value.length(), "\n", value));
  }

  void createUpstreams() override {
    ASSERT_EQ(upstreamProtocol(), Http::CodecType::HTTP3);
    ASSERT_EQ(fake_upstreams_count_, 1);
    ASSERT_FALSE(autonomous_upstream_);

    if (use_http2_) {
      auto config = configWithType(Http::CodecType::HTTP2);
      Network::TransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
      addFakeUpstream(std::move(factory), Http::CodecType::HTTP2);
    } else {
      auto config = configWithType(Http::CodecType::HTTP3);
      Network::TransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
      addFakeUpstream(std::move(factory), Http::CodecType::HTTP3);
      writeFile();
    }
  }

  bool use_http2_{false};
};

TEST_P(MixedUpstreamIntegrationTest, BasicRequestAutoWithHttp3) {
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(MixedUpstreamIntegrationTest, SimultaneousRequestsAutoWithHttp3) {
  simultaneousRequest(1024, 512, 1023, 513);
}

TEST_P(MixedUpstreamIntegrationTest, SimultaneousLargeRequestsAutoWithHttp3) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 20, 1024 * 14 + 2, 1024 * 10 + 5, 1024 * 16);
}

TEST_P(MixedUpstreamIntegrationTest, BasicRequestAutoWithHttp2) {
  use_http2_ = true;
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(MixedUpstreamIntegrationTest, SimultaneousRequestsAutoWithHttp2) {
  use_http2_ = true;
  simultaneousRequest(1024, 512, 1023, 513);
}

TEST_P(MixedUpstreamIntegrationTest, SimultaneousLargeRequestsAutoWithHttp2) {
  use_http2_ = true;
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 20, 1024 * 14 + 2, 1024 * 10 + 5, 1024 * 16);
}

INSTANTIATE_TEST_SUITE_P(Protocols, MixedUpstreamIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

#endif

} // namespace
} // namespace Envoy
