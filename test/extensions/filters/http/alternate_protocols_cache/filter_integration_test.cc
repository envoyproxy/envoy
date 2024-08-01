#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace {

#ifdef ENVOY_ENABLE_QUIC

// This tests the alternative service filter getting updated, by creating both
// HTTP/2 and HTTP/3 upstreams, and having the HTTP/2 upstream direct Envoy to
// the HTTP/3 upstream using alt-svc response headers.
class FilterIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void initialize() override {
    TestEnvironment::writeStringToFileForTest("alt_svc_cache.txt", "");
    const std::string filename = TestEnvironment::temporaryPath("alt_svc_cache.txt");
    envoy::config::core::v3::AlternateProtocolsCacheOptions alt_cache;
    alt_cache.set_name("default_alternate_protocols_cache");
    envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig config;
    config.set_filename(filename);
    config.mutable_flush_interval()->set_nanos(0);
    envoy::config::common::key_value::v3::KeyValueStoreConfig kv_config;
    kv_config.mutable_config()->set_name("envoy.key_value.file_based");
    kv_config.mutable_config()->mutable_typed_config()->PackFrom(config);
    alt_cache.mutable_key_value_store_config()->set_name("envoy.common.key_value");
    alt_cache.mutable_key_value_store_config()->mutable_typed_config()->PackFrom(kv_config);

    const std::string filter = fmt::format(R"EOF(
name: alternate_protocols_cache
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig
)EOF",
                                           filename);
    config_helper_.prependFilter(filter);

    // Set resource tracking on connection pools so we can explicitly check when
    // they're created and torn down.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);
      auto* circuit_breakers = cluster->mutable_circuit_breakers();
      circuit_breakers->add_thresholds()->mutable_max_connection_pools()->set_value(100);
      circuit_breakers->mutable_thresholds(0)->set_track_remaining(true);
    });

    upstream_tls_ = true;
    // This configures the upstream to use the connection grid (automatically
    // selecting protocol and allowing HTTP/3)
    config_helper_.configureUpstreamTls(/*use_alpn=*/true, /*http3=*/true, alt_cache);

    HttpProtocolIntegrationTest::initialize();
  }

  void writeAltSvcToFile(uint32_t index) {
    uint32_t port = fake_upstreams_[index]->localAddress()->ip()->port();
    std::string key = absl::StrCat("https://sni.lyft.com:", port);

    size_t seconds = std::chrono::duration_cast<std::chrono::seconds>(
                         timeSystem().monotonicTime().time_since_epoch())
                         .count();
    std::string value = absl::StrCat("h3=\":", port, "\"; ma=", 86400 + seconds, "|0|0");
    TestEnvironment::writeStringToFileForTest(
        "alt_svc_cache.txt", absl::StrCat(key.length(), "\n", key, value.length(), "\n", value));
  }

  // This function will create 2 upstreams, but Envoy will only point at the
  // first, the HTTP/2 upstream.
  void createUpstreams() override {
    // The test is configured for one upstream (Envoy will only point to the HTTP/2 upstream) but
    // we create two. Tell the test framework this is intentional.
    skipPortUsageValidation();
    ASSERT_FALSE(autonomous_upstream_);

    // Until alt-svc supports different ports, try to get a TCP and UDP fake upstream on the same
    // port.
    for (int i = 0; i < 10; ++i) {
      TRY_ASSERT_MAIN_THREAD {
        // Make the first upstream HTTP/2
        auto http2_config = configWithType(Http::CodecType::HTTP2);
        Network::DownstreamTransportSocketFactoryPtr http2_factory =
            createUpstreamTlsContext(http2_config);
        addFakeUpstream(std::move(http2_factory), Http::CodecType::HTTP2,
                        /*autonomous_upstream=*/false);

        // Make the next upstream is HTTP/3
        auto http3_config = configWithType(Http::CodecType::HTTP3);
        Network::DownstreamTransportSocketFactoryPtr http3_factory =
            createUpstreamTlsContext(http3_config);
        // If the UDP port is in use, this will throw an exception and get caught below.
        fake_upstreams_.emplace_back(std::make_unique<FakeUpstream>(
            std::move(http3_factory), fake_upstreams_[0]->localAddress()->ip()->port(), version_,
            http3_config));
        if (write_alt_svc_to_file_) {
          writeAltSvcToFile(1);
        }
        return;
      }
      END_TRY
      catch (const EnvoyException& e) {
        fake_upstreams_.clear();
        ENVOY_LOG_MISC(warn, "Failed to use port {}",
                       fake_upstreams_[0]->localAddress()->ip()->port());
      }
    }
    throw EnvoyException("Failed to find a port after 10 tries");
  }
  // If this is set to true, it pre-fills the alt-svc cache, setting up HTTP/3
  // support and pointing at the HTTP/3 upstream.
  bool write_alt_svc_to_file_ = false;
};

TEST_P(FilterIntegrationTest, AltSvc) {
  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},       {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  int port = fake_upstreams_[1]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/2 (upstream index 0). The response includes an
  // Alt-Svc header.
  auto response = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response.get());

  // Close the connection so the HTTP/2 connection will not be used.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 1);
  ASSERT_TRUE(fake_upstream_connection_->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  fake_upstream_connection_.reset();

  // Second request should go out over HTTP/3 (upstream index 1) because of the Alt-Svc information.
  // This could arguably flake due to the race, at which point request #2 should go to {0, 1}
  // but for now it seems to pass.
  auto response2 = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                 response_size, 1, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response2.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 1);
}

TEST_P(FilterIntegrationTest, AltSvcCached) {
  // Start with the alt-svc header in the cache.
  write_alt_svc_to_file_ = true;

  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},       {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  int port = fake_upstreams_[1]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/3 (upstream index 1) because of the Alt-Svc information.
  auto response2 = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                 response_size, 1, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response2.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 1);
}

TEST_P(FilterIntegrationTest, AltSvcCachedH3Slow) {
#ifdef WIN32
  GTEST_SKIP() << "Skipping on Windows";
#endif
  // Start with the alt-svc header in the cache.
  write_alt_svc_to_file_ = true;

  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  absl::Notification block_until_notify;
  // Block the H3 upstream so it can't process packets.
  fake_upstreams_[1]->runOnDispatcherThread([&] { block_until_notify.WaitForNotification(); });

  ASSERT(codec_client_ != nullptr);
  // Send the request to Envoy.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  // The request should fail over to the HTTP/2 upstream (index 0) as the H3 upstream is wedged.
  waitForNextUpstreamRequest(0);

  // Now unblock the HTTP/3 server and wait for the connection.
  block_until_notify.Notify();
  FakeHttpConnectionPtr h3_connection;
  waitForNextUpstreamConnection(std::vector<uint64_t>{1}, TestUtility::DefaultTimeout,
                                h3_connection);
  // Of the 100 connection pools configured, the grid registers as taking up one.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.remaining_cx_pools", 99);

  // The created stream will reset.
  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(h3_connection->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForReset());

  // Send a second request. This should go out over the H3 connection.
  FakeStreamPtr upstream_request3;
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(h3_connection->waitForNewStream(*dispatcher_, upstream_request3));
  upstream_request3->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream(timeout));

  // Now close the connection to make sure it doesn't cause problems for the
  // downstream stream.
  ASSERT_TRUE(h3_connection->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

  // Finish the response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  // Wait for the response to be read by the codec client.
  ASSERT_TRUE(response->waitForEndStream(timeout));

  checkSimpleRequestSuccess(request_size, response_size, response.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 1);

  cleanupUpstreamAndDownstream();
  // Wait for the grid to be torn down to make sure it is not problematic.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.remaining_cx_pools",
                               100);
}

// TODO(32151): Figure out why it's flaky and re-enable.
TEST_P(FilterIntegrationTest, DISABLED_AltSvcCachedH2Slow) {
#ifdef WIN32
  // TODO: sort out what race only happens on windows and GCC.
  GTEST_SKIP() << "Skipping on Windows";
#endif
#ifdef GCC_COMPILER
  GTEST_SKIP() << "Skipping on GCC";
#endif
  // Start with the alt-svc header in the cache.
  write_alt_svc_to_file_ = true;
  config_helper_.setConnectTimeout(std::chrono::seconds(1));

  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();

  absl::Notification block_http2;
  absl::Notification block_http3;
  absl::Notification http2_blocked;
  absl::Notification http3_blocked;
  // Block both upstreams.
  fake_upstreams_[0]->runOnDispatcherThread([&] {
    http2_blocked.Notify();
    block_http2.WaitForNotification();
  });
  fake_upstreams_[1]->runOnDispatcherThread([&] {
    http3_blocked.Notify();
    block_http3.WaitForNotification();
  });

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Make sure the upstreams are blocked.
  http2_blocked.WaitForNotification();
  http3_blocked.WaitForNotification();

  // Send the request to Envoy.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for both connections to be attempted.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 2);
  // Of the 100 connection pools configured, the grid registers as taking up one.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.remaining_cx_pools", 99);

  // Unblock Http3.
  block_http3.Notify();
  // Make sure the HTTP3 connection is established.
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Unblock the H2 upstream.
  block_http2.Notify();

  // Make sure the response is completed.
  ASSERT_TRUE(response->waitForEndStream(timeout));
  checkSimpleRequestSuccess(request_size, response_size, response.get());

  cleanupUpstreamAndDownstream();
  // Wait for the grid to be torn down to make sure it is not problematic.
  test_server_->waitForGaugeEq("cluster.cluster_0.circuit_breakers.default.remaining_cx_pools",
                               100);
}

TEST_P(FilterIntegrationTest, AltSvcIgnoredWithProxyConfig) {
  config_helper_.addFilter("{ name: header-to-proxy-filter }");
  const uint64_t request_size = 0;
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},       {":path", "/test/long/url"},
      {":scheme", "http"},       {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
  int port = fake_upstreams_[1]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/2 (upstream index 0). The response includes an
  // Alt-Svc header.
  auto response = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response.get());

  // Close the connection so the HTTP/2 connection will not be used.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 1);
  ASSERT_TRUE(fake_upstream_connection_->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  fake_upstream_connection_.reset();

  absl::string_view upstream_address(fake_upstreams_[0]->localAddress()->asStringView());
  request_headers.setCopy(Envoy::Http::LowerCaseString("connect-proxy"), upstream_address);

  // Second request will still go to the HTTP/2 cluster, due to the presence of proxy config
  // and will go over TCP/TLS due to proxy config.
  auto response2 = sendRequestAndWaitForResponse(request_headers, request_size, response_headers,
                                                 response_size, 0, timeout);
  checkSimpleRequestSuccess(request_size, response_size, response2.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 2);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 0);
}

TEST_P(FilterIntegrationTest, RetryAfterHttp3ZeroRttHandshakeFailed) {
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;

  config_helper_.setConnectTimeout(std::chrono::seconds(2));

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  int port = fake_upstreams_[0]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/2. The response includes an Alt-Svc header.
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, response_headers, 0,
                                                /*upstream_index=*/0, timeout);
  checkSimpleRequestSuccess(0, response_size, response.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 1);
  // Close the connection so the HTTP/2 connection will not be used.
  ASSERT_TRUE(fake_upstream_connection_->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  fake_upstream_connection_.reset();

  // The 2nd request should go out over HTTP/3 because of the Alt-Svc information.
  auto response2 = sendRequestAndWaitForResponse(default_request_headers_, 0,
                                                 default_response_headers_, response_size,
                                                 /*upstream_index=*/1, timeout);
  checkSimpleRequestSuccess(0, response_size, response2.get());
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_cx_http3_total")->value());
  // Close the h3 upstream connection so that the next request will create another connection.
  ASSERT_TRUE(fake_upstream_connection_->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 2);
  fake_upstream_connection_.reset();

  // Stop the HTTP/3 fake upstream.
  fake_upstreams_[1]->cleanUp();

  // The 3rd request should be sent over HTTP/3 as early data because of the cached 0-RTT
  // credentials.
  auto response3 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  // Wait for the upstream to connect timeout and the failed early data request to be retried.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_retry", 1);
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_rq_0rtt")->value());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 3);

  // The retry should attempt both HTTP/3 and HTTP/2. And the TCP connection will win the race.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response3->waitForEndStream());
  checkSimpleRequestSuccess(0, response_size, response3.get());
  EXPECT_EQ(2u, test_server_->counter("cluster.cluster_0.upstream_cx_http2_total")->value());

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_connect_fail", 2);
  EXPECT_EQ(3u, test_server_->counter("cluster.cluster_0.upstream_cx_http3_total")->value());
  EXPECT_EQ(1u, test_server_->counter("cluster.cluster_0.upstream_http3_broken")->value());

  upstream_request_.reset();
  // As HTTP/3 is marked broken, the following request shouldn't cause the grid to attempt HTTP/3 to
  // upstream at all.
  auto response4 = sendRequestAndWaitForResponse(default_request_headers_, 0,
                                                 default_response_headers_, response_size,
                                                 /*upstream_index=*/0, timeout);
  checkSimpleRequestSuccess(0, response_size, response4.get());

  EXPECT_EQ(3u, test_server_->counter("cluster.cluster_0.upstream_cx_http3_total")->value());
}

TEST_P(FilterIntegrationTest, H3PostHandshakeFailoverToTcp) {
  const uint64_t response_size = 0;
  const std::chrono::milliseconds timeout = TestUtility::DefaultTimeout;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        route->mutable_per_request_buffer_limit_bytes()->set_value(4096);
      });

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", "sni.lyft.com"},
      {"x-lyft-user-id", "123"},
      {"x-forwarded-for", "10.0.0.1"},
      {"x-envoy-retry-on", "http3-post-connect-failure"}};
  int port = fake_upstreams_[0]->localAddress()->ip()->port();
  std::string alt_svc = absl::StrCat("h3=\":", port, "\"; ma=86400");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"alt-svc", alt_svc}};

  // First request should go out over HTTP/2. The response includes an Alt-Svc header.
  auto response = sendRequestAndWaitForResponse(request_headers, 2048, response_headers, 0,
                                                /*upstream_index=*/0, timeout);
  checkSimpleRequestSuccess(2048, response_size, response.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 1);

  // Close the connection so the HTTP/2 connection will not be used.
  ASSERT_TRUE(fake_upstream_connection_->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  fake_upstream_connection_.reset();
  // Second request should go out over HTTP/3 because of the Alt-Svc information.
  auto response2 = codec_client_->makeRequestWithBody(request_headers, 2048);
  waitForNextUpstreamRequest(1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http3_total", 1);
  // Close the HTTP/3 connection before sending back response. This would cause an upstream reset.
  ASSERT_TRUE(fake_upstream_connection_->close());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 2);
  fake_upstream_connection_.reset();
  upstream_request_.reset();

  // The reset request should be retried over TCP.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());

  checkSimpleRequestSuccess(2048, response_size, response2.get());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_http2_total", 2);
}

INSTANTIATE_TEST_SUITE_P(Protocols, FilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// This tests the connection grid with pre-populated alt-svc entries, and either
// an HTTP/2 or an HTTP/3 upstream (but not both).
class MixedUpstreamIntegrationTest : public FilterIntegrationTest {
protected:
  MixedUpstreamIntegrationTest() { default_request_headers_.setHost("sni.lyft.com"); }

  void createUpstreams() override {
    ASSERT_EQ(upstreamProtocol(), Http::CodecType::HTTP3);
    ASSERT_EQ(fake_upstreams_count_, 1);
    ASSERT_FALSE(autonomous_upstream_);

    if (use_http2_) {
      auto config = configWithType(Http::CodecType::HTTP2);
      Network::DownstreamTransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
      addFakeUpstream(std::move(factory), Http::CodecType::HTTP2, /*autonomous_upstream=*/false);
    } else {
      auto config = configWithType(Http::CodecType::HTTP3);
      Network::DownstreamTransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
      addFakeUpstream(std::move(factory), Http::CodecType::HTTP3, /*autonomous_upstream=*/false);
      writeAltSvcToFile(0);
    }
  }

  bool use_http2_{false};
};

int getSrtt(std::string alt_svc, TimeSource& time_source) {
  auto data = Http::HttpServerPropertiesCacheImpl::originDataFromString(alt_svc, time_source,
                                                                        /*from_cache=*/false);
  return data.has_value() ? data.value().srtt.count() : 0;
}

// Test auto-config with a pre-populated HTTP/3 alt-svc entry. The upstream request will
// occur over HTTP/3.
TEST_P(MixedUpstreamIntegrationTest, BasicRequestAutoWithHttp3) {
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
  cleanupUpstreamAndDownstream();
  std::string alt_svc;

  // Make sure the srtt gets updated to a non-zero value.
  for (int i = 0; i < 5; ++i) {
    // Make sure that srtt is updated.
    const std::string filename = TestEnvironment::temporaryPath("alt_svc_cache.txt");
    alt_svc = TestEnvironment::readFileToStringForTest(filename);
    if (getSrtt(alt_svc, timeSystem()) != 0) {
      break;
    }
    timeSystem().advanceTimeWait(std::chrono::milliseconds(10));
  }
  EXPECT_NE(getSrtt(alt_svc, timeSystem()), 0) << alt_svc;
}

// Test simultaneous requests using auto-config and a pre-populated HTTP/3 alt-svc entry. The
// upstream request will occur over HTTP/3.
TEST_P(MixedUpstreamIntegrationTest, SimultaneousRequestsAutoWithHttp3) {
  simultaneousRequest(1024, 512, 1023, 513);
}

// Test large simultaneous requests using auto-config and a pre-populated HTTP/3 alt-svc entry. The
// upstream request will occur over HTTP/3.
TEST_P(MixedUpstreamIntegrationTest, SimultaneousLargeRequestsAutoWithHttp3) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  simultaneousRequest(1024 * 20, 1024 * 14 + 2, 1024 * 10 + 5, 1024 * 16);
}

// Test auto-config with a pre-populated HTTP/3 alt-svc entry. With the HTTP/3 upstream "disabled"
// the upstream request will occur over HTTP/3.
TEST_P(MixedUpstreamIntegrationTest, BasicRequestAutoWithHttp2) {
  // Only create an HTTP/2 upstream.
  use_http2_ = true;
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0, 0);
}

// Same as above, only multiple requests.
TEST_P(MixedUpstreamIntegrationTest, SimultaneousRequestsAutoWithHttp2) {
  use_http2_ = true;
  simultaneousRequest(1024, 512, 1023, 513);
}

// Same as above, only large multiple requests.
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
