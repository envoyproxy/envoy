#include "protocol_integration_test.h"
#include "test/integration/protocol_integration_test.h"

#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/api/api_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread_annotations.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/common/upstream/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/socket_interface_swap.h"
#include "test/integration/test_host_predicate_config.h"
#include "test/integration/utility.h"
#include "test/mocks/upstream/retry_priority.h"
#include "test/mocks/upstream/retry_priority_factory.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "gtest/gtest.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#endif

using testing::HasSubstr;
using testing::Not;

namespace Envoy {

void setDoNotValidateRouteConfig(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  auto* route_config = hcm.mutable_route_config();
  route_config->mutable_validate_clusters()->set_value(false);
};

TEST_P(ProtocolIntegrationTest, TrailerSupportHttp1) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());

  testTrailers(10, 20, true, true);
}

TEST_P(ProtocolIntegrationTest, ShutdownWithActiveConnPoolConnections) {
  auto response = makeHeaderOnlyRequest(nullptr, 0);
  // Shut down the server with active connection pool connections.
  test_server_.reset();
  checkSimpleRequestSuccess(0U, 0U, response.get());
}

// Test upstream_rq_per_cx metric tracks requests per connection
TEST_P(ProtocolIntegrationTest, UpstreamRequestsPerConnectionMetric) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send 3 requests on the same connection
  for (int i = 0; i < 3; ++i) {
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  // Use the proper cleanup pattern that triggers histogram recording
  cleanupUpstreamAndDownstream();

  // Wait for the histogram to actually have samples using the proper integration test pattern
  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rq_per_cx");

  // Get the histogram and read values using the proper pattern
  auto histogram = test_server_->histogram("cluster.cluster_0.upstream_rq_per_cx");

  uint64_t sample_count =
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram);
  uint64_t sample_sum = TestUtility::readSampleSum(test_server_->server().dispatcher(), *histogram);

  // Should have 1 sample with value 3 (3 requests on 1 connection)
  EXPECT_EQ(sample_count, 1);
  EXPECT_EQ(sample_sum, 3);
}

// Test that upstream_rq_per_cx metric is NOT recorded when handshake fails
TEST_P(ProtocolIntegrationTest, UpstreamRequestsPerConnectionMetricHandshakeFailure) {
  // This test intentionally causes upstream connection failures, so bypass the upstream validation
  testing_upstream_intentionally_ = true;

  // Configure upstream with invalid port to force connection failure before handshake completion
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* lb_endpoint =
        cluster->mutable_load_assignment()->mutable_endpoints(0)->mutable_lb_endpoints(0);
    // Use port 1 which is invalid/inaccessible to force connection establishment failure
    lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(1);
  });
  config_helper_.skipPortUsageValidation();

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request that will fail due to upstream connection failure
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for response (should fail with 503 Service Unavailable)
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  // Clean up
  codec_client_->close();

  // Wait for connection failure to be recorded
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_connect_fail", 1);

  // Verify that NO upstream_rq_per_cx histogram samples were recorded
  // because hasHandshakeCompleted() returned false (connection never established)
  auto histogram = test_server_->histogram("cluster.cluster_0.upstream_rq_per_cx");
  uint64_t sample_count =
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram);

  // Key assertion: No histogram samples should be recorded for failed connections
  EXPECT_EQ(sample_count, 0);

  // Also verify connection failure was recorded (proving connection attempt was made)
  EXPECT_GE(test_server_->counter("cluster.cluster_0.upstream_cx_connect_fail")->value(), 1);
}

TEST_P(ProtocolIntegrationTest, LogicalDns) {
  if (use_universal_header_validator_) {
    // TODO(#27132): auto_host_rewrite is broken for IPv6 and is failing UHV validation
    return;
  }
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::LOGICAL_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
    auto* typed_dns_resolver_config = cluster.mutable_typed_dns_resolver_config();
    typed_dns_resolver_config->set_name("envoy.network.dns_resolver.getaddrinfo");
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
        getaddrinfo_config;
    typed_dns_resolver_config->mutable_typed_config()->PackFrom(getaddrinfo_config);
  });
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        route->mutable_route()->mutable_auto_host_rewrite()->set_value(true);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ProtocolIntegrationTest, StrictDns) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::STRICT_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
    auto* typed_dns_resolver_config = cluster.mutable_typed_dns_resolver_config();
    typed_dns_resolver_config->set_name("envoy.network.dns_resolver.getaddrinfo");
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
        getaddrinfo_config;
    typed_dns_resolver_config->mutable_typed_config()->PackFrom(getaddrinfo_config);
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Change the default route to be restrictive, and send a request to an alternate route.
TEST_P(DownstreamProtocolIntegrationTest, RouterNotFound) { testRouterNotFound(); }

TEST_P(ProtocolIntegrationTest, RouterVirtualClusters) { testRouterVirtualClusters(); }

TEST_P(ProtocolIntegrationTest, RouterStats) { testRouteStats(); }

// Change the default route to be restrictive, and send a POST to an alternate route.
TEST_P(DownstreamProtocolIntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody();
}

// Add a route that uses unknown cluster (expect 404 Not Found).
TEST_P(DownstreamProtocolIntegrationTest, RouterClusterNotFound404) {
  config_helper_.addConfigModifier(&setDoNotValidateRouteConfig);
  config_helper_.addConfigModifier(configureProxyStatus());
  auto host = config_helper_.createVirtualHost("foo.lyft.com", "/unknown", "unknown_cluster");
  host.mutable_routes(0)->mutable_route()->set_cluster_not_found_response_code(
      envoy::config::route::v3::RouteAction::NOT_FOUND);
  config_helper_.addVirtualHost(host);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/unknown", "", downstream_protocol_, version_, "foo.lyft.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_EQ(response->headers().getProxyStatusValue(),
            "envoy; error=destination_unavailable; details=\"cluster_not_found; NC\"");
}

TEST_P(DownstreamProtocolIntegrationTest, TestHostWhitespacee) {
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(&setDoNotValidateRouteConfig);
  auto host = config_helper_.createVirtualHost("foo.lyft.com", "/unknown", "unknown_cluster");
  host.mutable_routes(0)->mutable_route()->set_cluster_not_found_response_code(
      envoy::config::route::v3::RouteAction::NOT_FOUND);
  config_helper_.addVirtualHost(host);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":authority", " foo.lyft.com "}, {":path", "/unknown"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // For HTTP/1 the whitespace will be stripped, and 404 returned as above.
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("404", response->headers().getStatusValue());
    EXPECT_TRUE(response->complete());
  } else {
    // For HTTP/2 and above, the whitespace is illegal.
    ASSERT_TRUE(response->waitForReset());
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
}

// Add a route that uses unknown cluster (expect 503 Service Unavailable).
TEST_P(DownstreamProtocolIntegrationTest, RouterClusterNotFound503) {
  config_helper_.addConfigModifier(&setDoNotValidateRouteConfig);
  auto host = config_helper_.createVirtualHost("foo.lyft.com", "/unknown", "unknown_cluster");
  host.mutable_routes(0)->mutable_route()->set_cluster_not_found_response_code(
      envoy::config::route::v3::RouteAction::SERVICE_UNAVAILABLE);
  config_helper_.addVirtualHost(host);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/unknown", "", downstream_protocol_, version_, "foo.lyft.com");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Add a route which redirects HTTP to HTTPS, and verify Envoy sends a 301
TEST_P(DownstreamProtocolIntegrationTest, RouterRedirectHttpRequest) {
  autonomous_upstream_ = true;
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  auto host = config_helper_.createVirtualHost("www.redirect.com", "/");
  host.set_require_tls(envoy::config::route::v3::VirtualHost::ALL);
  config_helper_.addVirtualHost(host);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/foo", "", downstream_protocol_, version_, "www.redirect.com");
  ASSERT_TRUE(response->complete());
  if (downstream_protocol_ <= Http::CodecType::HTTP2) {
    EXPECT_EQ("301", response->headers().getStatusValue());
    EXPECT_EQ("https://www.redirect.com/foo",
              response->headers().get(Http::Headers::get().Location)[0]->value().getStringView());
    expectDownstreamBytesSentAndReceived(BytesCountExpectation(145, 45, 111, 23),
                                         BytesCountExpectation(69, 30, 69, 30),
                                         BytesCountExpectation(0, 30, 0, 30));
  } else {
    // All QUIC requests use https, and should not be redirected. (Even those sent with http scheme
    // will be overridden to https by HCM.)
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(ProtocolIntegrationTest, UnknownResponsecode) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "600"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, response_headers, 0);

  // Regression test https://github.com/envoyproxy/envoy/issues/14890 - no content-length added.
  EXPECT_EQ(upstream_request_->headers().ContentLength(), nullptr);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("600", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, AddInvalidDecodedData) {
  EXPECT_ENVOY_BUG(
      {
        useAccessLog("%RESPONSE_CODE_DETAILS%");
        config_helper_.prependFilter(R"EOF(
  name: add-invalid-data-filter
  )EOF");
        initialize();
        codec_client_ = makeHttpConnection(lookupPort("http"));
        auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
        waitForNextUpstreamRequest();
        upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
        ASSERT_TRUE(response->waitForEndStream());
        EXPECT_EQ("502", response->headers().getStatusValue());
        EXPECT_THAT(waitForAccessLog(access_log_name_),
                    HasSubstr("filter_added_invalid_request_data"));
      },
      "Invalid request data");
}

TEST_P(DownstreamProtocolIntegrationTest, AddInvalidEncodedData) {
  EXPECT_ENVOY_BUG(
      {
        useAccessLog("%RESPONSE_CODE_DETAILS%");
        config_helper_.prependFilter(R"EOF(
  name: add-invalid-data-filter
  )EOF");
        initialize();
        codec_client_ = makeHttpConnection(lookupPort("http"));
        default_request_headers_.setCopy(Envoy::Http::LowerCaseString("invalid-encode"), "yes");
        auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
        auto response = std::move(encoder_decoder.second);
        ASSERT_TRUE(response->waitForEndStream());
        EXPECT_EQ("502", response->headers().getStatusValue());
        EXPECT_THAT(waitForAccessLog(access_log_name_),
                    HasSubstr("filter_added_invalid_response_data"));
        cleanupUpstreamAndDownstream();
      },
      "Invalid response data");
}

// Verifies behavior for https://github.com/envoyproxy/envoy/pull/11248
TEST_P(ProtocolIntegrationTest, AddBodyToRequestAndWaitForIt) {
  config_helper_.prependFilter(R"EOF(
  name: wait-for-whole-request-and-response-filter
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ("body", upstream_request_->body().toString());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  // encode data, as we have a separate test for the transforming header only response.
  upstream_request_->encodeData(128, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ProtocolIntegrationTest, DEPRECATED_FEATURE_TEST(RouterOnlyTracing)) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        envoy::extensions::filters::http::router::v3::Router router_config;
        router_config.set_start_child_span(true);
        hcm.mutable_http_filters(0)->mutable_typed_config()->PackFrom(router_config);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
}

TEST_P(ProtocolIntegrationTest, AddBodyToResponseAndWaitForIt) {
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: wait-for-whole-request-and-response-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("body", response->body());
}

TEST_P(ProtocolIntegrationTest, ContinueHeadersOnlyInjectBodyFilter) {
  config_helper_.prependFilter(R"EOF(
  name: continue-headers-only-inject-body-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make sure that the body was injected to the request.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), "body");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the body was injected to the response.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "body");
}

TEST_P(ProtocolIntegrationTest, StopIterationHeadersInjectBodyFilter) {
  config_helper_.prependFilter(R"EOF(
  name: stop-iteration-headers-inject-body-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make sure that the body was injected to the request.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->body().toString(), "body");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the body was injected to the response.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "body");
}

TEST_P(ProtocolIntegrationTest, AddEncodedTrailers) {
  config_helper_.prependFilter(R"EOF(
name: add-trailers-filter
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
  upstream_request_->encodeData(128, true);
  ASSERT_TRUE(response->waitForEndStream());

  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    EXPECT_EQ("decode", upstream_request_->trailers()
                            ->get(Http::LowerCaseString("grpc-message"))[0]
                            ->value()
                            .getStringView());
  }
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    EXPECT_EQ("encode", response->trailers()->getGrpcMessageValue());
  }
}

TEST_P(ProtocolIntegrationTest, MixedCaseScheme) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setScheme("Http");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  auto scheme = upstream_request_->headers().getSchemeValue();
  EXPECT_TRUE(scheme.empty() || scheme == "http");
}

TEST_P(ProtocolIntegrationTest, SchemeTransformation) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_scheme_header_transformation()->set_scheme_to_overwrite("https");
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setScheme("Http");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  auto scheme = upstream_request_->headers().getSchemeValue();
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    EXPECT_EQ(scheme, "https");
  } else {
    // For H/1 upstreams the test fake upstream does not have :scheme, since it is set in HCM
    EXPECT_TRUE(scheme.empty());
  }
}

TEST_P(ProtocolIntegrationTest, AccessLogOnRequestStartTest) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_access_log_options()->set_flush_access_log_on_new_request(true);
      });

  useAccessLog("%RESPONSE_CODE% %ACCESS_LOG_TYPE%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host.com"}});
  waitForNextUpstreamRequest();
  EXPECT_EQ(absl::StrCat("0 ", AccessLogType_Name(AccessLog::AccessLogType::DownstreamStart)),
            waitForAccessLog(access_log_name_, 0, true));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(absl::StrCat("200 ", AccessLogType_Name(AccessLog::AccessLogType::DownstreamEnd)),
            waitForAccessLog(access_log_name_, 1, true));
}

TEST_P(ProtocolIntegrationTest, PeriodicAccessLog) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_access_log_options()->mutable_access_log_flush_interval()->set_nanos(
            100000000); // 0.1 seconds
      });

  useAccessLog("%ACCESS_LOG_TYPE%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host.com"}});
  waitForNextUpstreamRequest();
  EXPECT_EQ(AccessLogType_Name(AccessLog::AccessLogType::DownstreamPeriodic),
            waitForAccessLog(access_log_name_, 0, true));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/9873
TEST_P(ProtocolIntegrationTest, ResponseWithHostHeader) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"host", "host"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("host",
            response->headers().get(Http::LowerCaseString("host"))[0]->value().getStringView());
}

// Upstream 304 response with Content-Length and no actual body
// The content-length header should be preserved and no transfer-encoding header should be added
TEST_P(ProtocolIntegrationTest, Upstream304ResponseWithContentLength) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"if-none-match", "\"1234567890\""}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "304"},
                                                                   {"etag", "\"1234567890\""},
                                                                   {"content-length", "123"}},
                                   true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("304", response->headers().getStatusValue());
  EXPECT_EQ(
      "123",
      response->headers().get(Http::LowerCaseString("content-length"))[0]->value().getStringView());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("transfer-encoding")).empty());

  // Make a HEAD request to make sure the previous 304 response with Content-Length did not cause
  // issue.
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "HEAD"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"if-none-match", "\"1234567890\""}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "304"},
                                                                   {"etag", "\"1234567890\""},
                                                                   {"content-length", "123"}},
                                   true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("304", response->headers().getStatusValue());
  EXPECT_EQ(
      "123",
      response->headers().get(Http::LowerCaseString("content-length"))[0]->value().getStringView());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("transfer-encoding")).empty());
}

// Upstream 304 response without Content-Length
// No content-length nor transfer-encoding header should be added for all protocol combinations.
TEST_P(ProtocolIntegrationTest, 304ResponseWithoutContentLength) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"if-none-match", "\"1234567890\""}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "304"}, {"etag", "\"1234567890\""}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("304", response->headers().getStatusValue());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("transfer-encoding")).empty());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("content-length")).empty());
}

// Upstream 304 response for HEAD request without Content-Length
// Response to HEAD request is the same as response to GET request and consistent with different
// protocol combinations.
TEST_P(ProtocolIntegrationTest, 304HeadResponseWithoutContentLength) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "HEAD"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"if-none-match", "\"1234567890\""}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "304"}, {"etag", "\"1234567890\""}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("304", response->headers().getStatusValue());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("transfer-encoding")).empty());
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("content-length")).empty());
}

// Tests that the response to a HEAD request can have content-length header but empty body.
TEST_P(ProtocolIntegrationTest, 200HeadResponseWithContentLength) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "HEAD"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"if-none-match", "\"1234567890\""}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "123"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(
      "123",
      response->headers().get(Http::LowerCaseString("content-length"))[0]->value().getStringView());
}

// Tests missing headers needed for H/1 codec first line.
TEST_P(DownstreamProtocolIntegrationTest, DownstreamRequestWithFaultyFilter) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    // For QUIC, even through the headers are not sent upstream, the stream will
    // be created. Use the autonomous upstream and allow incomplete streams.
    autonomous_allow_incomplete_streams_ = true;
    autonomous_upstream_ = true;
  }
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::MatchesRegex(".*required.*header.*"));

  // Missing path for non-CONNECT
  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-path", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), testing::MatchesRegex(".*required.*header.*"));
}

TEST_P(DownstreamProtocolIntegrationTest, FaultyFilterWithConnect) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    // For QUIC, even through the headers are not sent upstream, the stream will
    // be created. Use the autonomous upstream and allow incomplete streams.
    autonomous_allow_incomplete_streams_ = true;
    autonomous_upstream_ = true;
  }
  // Faulty filter that removed host in a CONNECT request.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        ConfigHelper::setConnectConfig(hcm, false, false,
                                       downstreamProtocol() == Http::CodecType::HTTP3);
      });
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing host for CONNECT
  auto headers = Http::TestRequestHeaderMapImpl{
      {":method", "CONNECT"}, {":scheme", "http"}, {":authority", "www.host.com:80"}};
  auto encoder_decoder = codec_client_->startRequest(headers);
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // With and without UHV the details string is different:
  // non-UHV: filter_removed_required_request_headers{missing_required_header:_host}
  // UHV: filter_removed_required_request_headers{header_validation_failed:_uhv.invalid_host}
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::MatchesRegex(".*required.*header.*"));
  // Wait to process STOP_SENDING on the client for quic.
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_TRUE(response->waitForReset());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, MissingHeadersLocalReply) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid_header_filter_ready"));
}

TEST_P(DownstreamProtocolIntegrationTest, MissingHeadersLocalReplyDownstreamBytesCount) {
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  config_helper_.addFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  expectDownstreamBytesSentAndReceived(BytesCountExpectation(90, 88, 71, 54),
                                       BytesCountExpectation(40, 58, 40, 58),
                                       BytesCountExpectation(7, 10, 7, 8));
}

TEST_P(DownstreamProtocolIntegrationTest, MissingHeadersLocalReplyUpstreamBytesCount) {
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
  config_helper_.addFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  expectUpstreamBytesSentAndReceived(BytesCountExpectation(0, 0, 0, 0),
                                     BytesCountExpectation(0, 0, 0, 0),
                                     BytesCountExpectation(0, 0, 0, 0));
}

TEST_P(DownstreamProtocolIntegrationTest, MissingHeadersLocalReplyWithBody) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}},
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid_header_filter_ready"));
}

TEST_P(DownstreamProtocolIntegrationTest, MissingHeadersLocalReplyWithBodyBytesCount) {
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  config_helper_.addFilter("{ name: invalid-header-filter }");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Missing method
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"remove-method", "yes"},
                                     {"send-reply", "yes"}},
      1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  expectDownstreamBytesSentAndReceived(BytesCountExpectation(109, 1152, 90, 81),
                                       BytesCountExpectation(0, 58, 0, 58),
                                       BytesCountExpectation(7, 10, 7, 8));
}

TEST_P(DownstreamProtocolIntegrationTest, TeSanitization) {
  if (downstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  autonomous_upstream_ = true;
  default_request_headers_.setTE("gzip");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ("", upstream_headers->getTEValue());
}

TEST_P(DownstreamProtocolIntegrationTest, TeSanitizationTrailers) {
  if (downstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  autonomous_upstream_ = true;
  default_request_headers_.setTE("trailers");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ("trailers", upstream_headers->getTEValue());
}

TEST_P(DownstreamProtocolIntegrationTest, TeSanitizationTrailersMultipleValuesAndWeigthted) {
  if (downstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  autonomous_upstream_ = true;
  default_request_headers_.setTE("chunked;q=0.8  ,  trailers  ,deflate  ");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  EXPECT_TRUE(upstream_headers != nullptr);
  EXPECT_EQ("trailers", upstream_headers->getTEValue());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10270
TEST_P(ProtocolIntegrationTest, LongHeaderValueWithSpaces) {
  // Header with at least 20kb of spaces surrounded by non-whitespace characters to ensure that
  // dispatching is split across 2 dispatch calls. This threshold comes from Envoy preferring 16KB
  // reads, which the buffer rounds up to about 20KB when allocating slices in
  // Buffer::OwnedImpl::reserve().
  const std::string long_header_value_with_inner_lws = "v" + std::string(32 * 1024, ' ') + "v";
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"longrequestvalue", long_header_value_with_inner_lws}});
  waitForNextUpstreamRequest();
  EXPECT_EQ(long_header_value_with_inner_lws, upstream_request_->headers()
                                                  .get(Http::LowerCaseString("longrequestvalue"))[0]
                                                  ->value()
                                                  .getStringView());
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"host", "host"},
                                      {"longresponsevalue", long_header_value_with_inner_lws}},
      true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("host",
            response->headers().get(Http::LowerCaseString("host"))[0]->value().getStringView());
  EXPECT_EQ(long_header_value_with_inner_lws,
            response->headers()
                .get(Http::LowerCaseString("longresponsevalue"))[0]
                ->value()
                .getStringView());
}

TEST_P(ProtocolIntegrationTest, Retry) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.mutable_track_cluster_stats()->set_request_response_sizes(true);
  });
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                          std::chrono::milliseconds(500)));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
  Stats::Store& stats = test_server_->server().stats();
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    Stats::CounterSharedPtr counter = TestUtility::findCounter(
        stats, absl::StrCat("cluster.cluster_0.", upstreamProtocolStatsRoot(), ".tx_reset"));
    ASSERT_NE(nullptr, counter);
    EXPECT_EQ(1L, counter->value());
  }
  EXPECT_NE(nullptr,
            test_server_->counter(absl::StrCat("cluster.cluster_0.", upstreamProtocolStatsRoot(),
                                               ".dropped_headers_with_underscores")));

  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rq_headers_size");
  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rs_headers_size");

  auto find_histo_sample_count = [&](const std::string& name) -> uint64_t {
    for (auto& histogram : test_server_->histograms()) {
      if (histogram->name() == name) {
        return TestUtility::readSampleCount(test_server_->server().dispatcher(), *histogram);
      }
    }
    return 0;
  };

  EXPECT_EQ(find_histo_sample_count("cluster.cluster_0.upstream_rq_headers_size"), 2);
  EXPECT_EQ(find_histo_sample_count("cluster.cluster_0.upstream_rs_headers_size"), 2);

  // The two requests are sent with https scheme rather than http for QUIC downstream.
  const size_t quic_https_extra_bytes = (downstreamProtocol() == Http::CodecType::HTTP3 ? 2u : 0u);
  const size_t http2_header_bytes_received =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 24 : 27;
  expectUpstreamBytesSentAndReceived(
      BytesCountExpectation(2566 + quic_https_extra_bytes, 635, 430 + quic_https_extra_bytes, 54),
      BytesCountExpectation(2262, 548, 196, http2_header_bytes_received),
      BytesCountExpectation(2204, 520, 150, 6));
}

TEST_P(ProtocolIntegrationTest, RetryWithBodyLargerThanNetworkBuffer) {
  // Set the network buffer to be 16KB.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->listeners_size() >= 1, "");
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    listener->mutable_per_connection_buffer_limit_bytes()->set_value(16 * 1024);
  });
  // Set the request body buffer limit to be 1MB so it can retry requests up to 1MB.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_route_config()
            ->mutable_virtual_hosts(0)
            ->mutable_request_body_buffer_limit()
            ->set_value(1024 * 1024);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send a request with 64Kb body so it is larger than the network 16Kb buffer.
  constexpr uint32_t kRequestBodySize = 64 * 1024;
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      kRequestBodySize);
  waitForNextUpstreamRequest();
  // Note that 503 is sent with end_stream=false (response with body). This will cause Envoy to
  // reset the response because it knows it is going to retry the request and it does not need the
  // response body.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                          std::chrono::milliseconds(500)));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(kRequestBodySize, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

// Regression test to guarantee that buffering for retries and shadows doesn't double the body size.
// This test is actually irrelevant for QUIC, as this issue only shows up with header-only requests.
// QUIC will always send an empty data frame with FIN.
TEST_P(ProtocolIntegrationTest, RetryWithBuffer) {
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DEFAULT
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}});
  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request_->receivedData());
  EXPECT_EQ(upstream_request_->bodyLength(), 4);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(ProtocolIntegrationTest, RetryStreaming) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}});
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;

  // Send some data, but not the entire body.
  std::string data(1024, 'a');
  Buffer::OwnedImpl send1(data);
  encoder.encodeData(send1, false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send back an upstream failure.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // Wait for a retry. Ensure all data, both before and after the retry, is received.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Finish the request.
  std::string data2(512, 'b');
  Buffer::OwnedImpl send2(data2);
  encoder.encodeData(send2, true);
  std::string combined_request_data = data + data2;
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, combined_request_data));

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(combined_request_data.size(), upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

// Regression test https://github.com/envoyproxy/envoy/issues/11131
// Send complete response headers directing a retry and reset the stream to make
// sure that Envoy cleans up stream state correctly when doing a retry with
// complete response but incomplete request.
TEST_P(ProtocolIntegrationTest, RetryStreamingReset) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}});
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;

  // Send some data, but not the entire body.
  std::string data(1024, 'a');
  Buffer::OwnedImpl send1(data);
  encoder.encodeData(send1, false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send back an upstream failure and end stream. Make sure an immediate reset
  // doesn't cause problems. Schedule via the upstream_request_ dispatcher to ensure that the stream
  // still exists when encoding the reset stream.
  upstream_request_->postToConnectionThread([this]() {
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
    upstream_request_->encodeResetStream();
  });

  // Make sure the fake stream is reset.
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // Wait for a retry. Ensure all data, both before and after the retry, is received.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Finish the request.
  std::string data2(512, 'b');
  Buffer::OwnedImpl send2(data2);
  encoder.encodeData(send2, true);
  std::string combined_request_data = data + data2;
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, combined_request_data));

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(combined_request_data.size(), upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

TEST_P(ProtocolIntegrationTest, RetryStreamingCancelDueToBufferOverflow) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);

        route->mutable_request_body_buffer_limit()->set_value(1024);
        route->mutable_route()
            ->mutable_retry_policy()
            ->mutable_retry_back_off()
            ->mutable_base_interval()
            ->MergeFrom(
                ProtobufUtil::TimeUtil::MillisecondsToDuration(100000000)); // Effectively infinity.
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}});
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;

  // Send some data, but less than the buffer limit, and not end-stream
  std::string data(64, 'a');
  Buffer::OwnedImpl send1(data);
  encoder.encodeData(send1, false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Send back an upstream failure.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // Overflow the request buffer limit. Because the retry base interval is infinity, no
  // request will be in progress. This will cause the request to be aborted and an error
  // to be returned to the client.
  std::string data2(2048, 'b');
  Buffer::OwnedImpl send2(data2);
  encoder.encodeData(send2, false);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("507", response->headers().getStatusValue());
  test_server_->waitForCounterEq("cluster.cluster_0.retry_or_shadow_abandoned", 1);
  cleanupUpstreamAndDownstream();
}

// Tests that the x-envoy-attempt-count header is properly set on the upstream request and the
// downstream response, and updated after the request is retried.
TEST_P(DownstreamProtocolIntegrationTest, RetryAttemptCountHeader) {
  auto host = config_helper_.createVirtualHost("sni.lyft.com", "/test_retry");
  host.set_include_request_attempt_count(true);
  host.set_include_attempt_count_in_response(true);
  config_helper_.addVirtualHost(host);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test_retry"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  EXPECT_EQ(atoi(std::string(upstream_request_->headers().getEnvoyAttemptCountValue()).c_str()), 1);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  EXPECT_EQ(atoi(std::string(upstream_request_->headers().getEnvoyAttemptCountValue()).c_str()), 2);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
  EXPECT_EQ(2, atoi(std::string(response->headers().getEnvoyAttemptCountValue()).c_str()));
}

// Verifies that a retry priority can be configured and affect the host selected during retries.
// The retry priority will always target P1, which would otherwise never be hit due to P0 being
// healthy.
TEST_P(DownstreamProtocolIntegrationTest, RetryPriority) {
  const Upstream::HealthyLoad healthy_priority_load({0u, 100u});
  const Upstream::DegradedLoad degraded_priority_load({0u, 100u});
  NiceMock<Upstream::MockRetryPriority> retry_priority(healthy_priority_load,
                                                       degraded_priority_load);
  Upstream::MockRetryPriorityFactory factory(retry_priority);

  Registry::InjectFactory<Upstream::RetryPriorityFactory> inject_factory(factory);

  // Add route with custom retry policy
  auto host = config_helper_.createVirtualHost("sni.lyft.com", "/test_retry");
  host.set_include_request_attempt_count(true);
  auto retry_policy = host.mutable_routes(0)->mutable_route()->mutable_retry_policy();
  retry_policy->mutable_retry_priority()->set_name(factory.name());
  config_helper_.addVirtualHost(host);
  // We want to work with a cluster with two hosts.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto* load_assignment = cluster->mutable_load_assignment();
    load_assignment->clear_endpoints();

    for (int i = 0; i < 2; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->set_priority(i);
      locality->mutable_locality()->set_region("region");
      locality->mutable_locality()->set_zone("zone");
      locality->mutable_locality()->set_sub_zone("sub_zone" + std::to_string(i));
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    }
  });
  fake_upstreams_count_ = 2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test_retry"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);

  // Note how we're expecting each upstream request to hit the same upstream.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    // Make sure waitForNextUpstreamRequest waits for a new connection.
    fake_upstream_connection_.reset();
  }
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

//
// Verifies that a retry host filter can be configured and affect the host selected during retries.
// The predicate will keep track of the first host attempted, and attempt to route all requests to
// the same host. With a total of two upstream hosts, this should result in us continuously sending
// requests to the same host.
TEST_P(DownstreamProtocolIntegrationTest, RetryHostPredicateFilter) {
  TestHostPredicateFactory predicate_factory;
  Registry::InjectFactory<Upstream::RetryHostPredicateFactory> inject_factory(predicate_factory);

  // Add route with custom retry policy
  auto host = config_helper_.createVirtualHost("sni.lyft.com", "/test_retry");
  host.set_include_request_attempt_count(true);
  auto retry_policy = host.mutable_routes(0)->mutable_route()->mutable_retry_policy();
  retry_policy->add_retry_host_predicate()->set_name(predicate_factory.name());
  config_helper_.addVirtualHost(host);

  // We want to work with a cluster with two hosts.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->mutable_load_assignment()
        ->mutable_endpoints(0)
        ->add_lb_endpoints()
        ->mutable_endpoint()
        ->MergeFrom(ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
  });
  fake_upstreams_count_ = 2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test_retry"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024);

  // Note how we're expecting each upstream request to hit the same upstream.
  auto upstream_idx = waitForNextUpstreamRequest({0, 1});
  ASSERT_TRUE(upstream_idx.has_value());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[*upstream_idx]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[*upstream_idx]->waitForHttpConnection(*dispatcher_,
                                                                      fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  waitForNextUpstreamRequest(*upstream_idx);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());
}

// Very similar set-up to testRetry but with a 65k request the request will not
// be buffered and the 503 will be returned to the user.
TEST_P(ProtocolIntegrationTest, RetryHittingBufferLimit) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(66560U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Very similar set-up to RetryHittingBufferLimits but using the route specific cap.
TEST_P(ProtocolIntegrationTest, RetryHittingRouteLimits) {
  auto host = config_helper_.createVirtualHost("routelimit.lyft.com", "/");
  host.mutable_request_body_buffer_limit()->set_value(0);
  config_helper_.addVirtualHost(host);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "routelimit.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Test hitting the decoder buffer filter with too many request bytes to buffer. Ensure the
// connection manager sends a 413.
TEST_P(DownstreamProtocolIntegrationTest, HittingDecoderFilterLimit) {
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);

  ASSERT_TRUE(response->waitForEndStream());
  // With HTTP/1 there's a possible race where if the connection backs up early,
  // the 413-and-connection-close may be sent while the body is still being
  // sent, resulting in a write error and the connection being closed before the
  // response is read.
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
  }
  if (response->complete()) {
    EXPECT_EQ("413", response->headers().getStatusValue());
  }
}

// Test hitting the decoder buffer filter with too many request bytes to buffer without end stream.
// Ensure the connection manager sends a 413.
TEST_P(DownstreamProtocolIntegrationTest, HittingDecoderFilterLimitNoEndStream) {
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  // With HTTP/1 there's a possible race where if the connection backs up early,
  // the 413-and-connection-close may be sent while the body is still being
  // sent, resulting in a write error and the connection being closed before the
  // response is read.
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
  }
  if (response->complete()) {
    EXPECT_EQ("413", response->headers().getStatusValue());
  }
}

// Test hitting the encoder buffer filter with too many response bytes to buffer. Given the request
// headers are sent on early, the stream/connection will be reset.
TEST_P(ProtocolIntegrationTest, HittingEncoderFilterLimit) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* header = virtual_host->mutable_response_headers_to_add()->Add()->mutable_header();
        header->set_key("foo");
        header->set_value("bar");
      });

  useAccessLog();
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl data("HTTP body content goes here");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  upstream_request_->encodeData(1024 * 65, false);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
  // Regression test all sendLocalReply paths add route-requested headers.
  auto foo = Http::LowerCaseString("foo");
  ASSERT_FALSE(response->headers().get(foo).empty());
  EXPECT_EQ("bar", response->headers().get(foo)[0]->value().getStringView());

  // Regression test https://github.com/envoyproxy/envoy/issues/9881 by making
  // sure this path does standard HCM header transformations.
  EXPECT_TRUE(response->headers().Date() != nullptr);
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("500"));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_5xx", 1);
}

// The downstream connection is closed when it is read disabled, and on OSX the
// connection error is not detected under these circumstances.
#if !defined(__APPLE__)
TEST_P(ProtocolIntegrationTest, 1xxAndClose) { testEnvoyHandling1xx(false, "", true); }
#endif

TEST_P(ProtocolIntegrationTest, EnvoyHandling1xx) { testEnvoyHandling1xx(); }

TEST_P(ProtocolIntegrationTest, EnvoyHandlingDuplicate1xx) { testEnvoyHandling1xx(true); }

// 100-continue before the request completes.
TEST_P(ProtocolIntegrationTest, EnvoyProxyingEarly1xx) { testEnvoyProxying1xx(true); }

// Multiple 1xx before the request completes.
TEST_P(ProtocolIntegrationTest, EnvoyProxyingEarlyMultiple1xx) {
  testEnvoyProxying1xx(true, false, true);
}

// 100-continue after the request completes.
TEST_P(ProtocolIntegrationTest, EnvoyProxyingLate1xx) { testEnvoyProxying1xx(false); }

// Multiple 1xx after the request completes.
TEST_P(ProtocolIntegrationTest, EnvoyProxyingLateMultiple1xx) {
  testEnvoyProxying1xx(false, false, true);
}

TEST_P(ProtocolIntegrationTest, EnvoyProxying102) {
  testEnvoyProxying1xx(false, false, false, "102");
}

TEST_P(ProtocolIntegrationTest, EnvoyProxying103) {
  testEnvoyProxying1xx(false, false, false, "103");
}

TEST_P(ProtocolIntegrationTest, EnvoyProxying104) {
  testEnvoyProxying1xx(false, false, false, "104");
}

TEST_P(DownstreamProtocolIntegrationTest, EnvoyProxying102DelayBalsaReset) {
  if (GetParam().upstream_protocol != Http::CodecType::HTTP1 ||
      GetParam().downstream_protocol != Http::CodecType::HTTP1) {
    GTEST_SKIP() << "This test is only relevant for HTTP1 BalsaParser";
  }
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http1_balsa_delay_reset", "true");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "HEAD"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"expect", "100-contINUE"}});
  waitForNextUpstreamRequest();
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "102"}});
  response->waitFor1xxHeaders();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  EXPECT_TRUE(response->waitForEndStream());
  // The client balsa parser has done a local reset.
  EXPECT_EQ(response->resetReason(), Http::StreamResetReason::LocalReset);

  cleanupUpstreamAndDownstream();
}

TEST_P(DownstreamProtocolIntegrationTest, EnvoyProxying102DelayBalsaResetWaitForFirstByte) {
  if (GetParam().upstream_protocol != Http::CodecType::HTTP1) {
    GTEST_SKIP() << "This test is only relevant for HTTP1 upstream with BalsaParser";
  }
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http1_balsa_delay_reset", "true");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "HEAD"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"expect", "100-contINUE"}});
  waitForNextUpstreamRequest();
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "102"}});
  response->waitFor1xxHeaders();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(DownstreamProtocolIntegrationTest, EnvoyProxying102NoDelayBalsaReset) {
  if (GetParam().upstream_protocol != Http::CodecType::HTTP1) {
    GTEST_SKIP() << "This test is only relevant for HTTP1 upstream with BalsaParser";
  }
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http1_balsa_delay_reset", "false");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "HEAD"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"expect", "100-contINUE"}});

  waitForNextUpstreamRequest();
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "102"}});
  response->waitFor1xxHeaders();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(ProtocolIntegrationTest, TwoRequests) { testTwoRequests(); }

TEST_P(ProtocolIntegrationTest, TwoRequestsWithForcedBackup) { testTwoRequests(true); }

TEST_P(ProtocolIntegrationTest, BasicMaxStreamDuration) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_stream_duration()->MergeFrom(
        ProtobufUtil::TimeUtil::MillisecondsToDuration(200));
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response->complete());
  } else {
    ASSERT_TRUE(response->waitForEndStream());
    codec_client_->close();
  }
}

TEST_P(ProtocolIntegrationTest, BasicDynamicMaxStreamDuration) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setEnvoyUpstreamStreamDurationMs(500);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response->complete());
  } else {
    ASSERT_TRUE(response->waitForEndStream());
    codec_client_->close();
  }
}

TEST_P(ProtocolIntegrationTest, MaxStreamDurationWithRetryPolicy) {
  if (upstreamProtocol() == Http::CodecType::HTTP3 ||
      downstreamProtocol() == Http::CodecType::HTTP3) {
    return; // TODO(#26236) Fix this test for H3.
  }
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_stream_duration()->MergeFrom(
        ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  Http::TestRequestHeaderMapImpl retriable_header =
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(retriable_header);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  response->waitForHeaders();
  codec_client_->close();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ProtocolIntegrationTest, MaxStreamDurationWithRetryPolicyWhenRetryUpstreamDisconnection) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    auto* http_protocol_options = protocol_options.mutable_common_http_protocol_options();
    http_protocol_options->mutable_max_stream_duration()->MergeFrom(
        ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  Http::TestRequestHeaderMapImpl retriable_header =
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(retriable_header);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 2);
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response->complete());
  } else {
    ASSERT_TRUE(response->waitForEndStream());
    codec_client_->close();
  }

  EXPECT_EQ("408", response->headers().getStatusValue());
}

// Verify that empty trailers are not sent as trailers.
TEST_P(DownstreamProtocolIntegrationTest, EmptyTrailersAreNotEncoded) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  config_helper_.prependFilter(R"EOF(
name: remove-response-trailers-filter
)EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData("b", false);
  Http::TestResponseTrailerMapImpl removed_trailers{{"some-trailer", "removed-by-filter"}};
  upstream_request_->encodeTrailers(removed_trailers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->trailers(), testing::IsNull());
}

// Verify that headers with underscores in their names are dropped from client requests
// but remain in upstream responses.
TEST_P(ProtocolIntegrationTest, HeadersWithUnderscoresDropped) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_common_http_protocol_options()->set_headers_with_underscores_action(
            envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"foo_bar", "baz"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendTrailers(
      *request_encoder_,
      Http::TestRequestTrailerMapImpl{{"trailer1", "value1"}, {"trailer_2", "value2"}});
  waitForNextUpstreamRequest();

  EXPECT_THAT(upstream_request_->headers(), Not(ContainsHeader("foo_bar", "baz")));
  // Headers with underscores should be dropped from request headers and trailers.
  EXPECT_THAT(*upstream_request_->trailers(), Not(ContainsHeader("trailer_2", "value2")));
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"bar_baz", "fooz"}}, false);
  upstream_request_->encodeData("b", false);
  upstream_request_->encodeTrailers(
      Http::TestResponseTrailerMapImpl{{"trailer1", "value1"}, {"response_trailer", "ok"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Both response headers and trailers must retain headers with underscores.
  EXPECT_THAT(response->headers(), ContainsHeader("bar_baz", "fooz"));
  EXPECT_THAT(*response->trailers(), ContainsHeader("response_trailer", "ok"));
  Stats::Store& stats = test_server_->server().stats();
  std::string stat_name;
  switch (downstreamProtocol()) {
  case Http::CodecType::HTTP1:
    stat_name = "http1.dropped_headers_with_underscores";
    break;
  case Http::CodecType::HTTP2:
    stat_name = "http2.dropped_headers_with_underscores";
    break;
  case Http::CodecType::HTTP3:
    stat_name = "http3.dropped_headers_with_underscores";
    break;
  default:
    RELEASE_ASSERT(false, fmt::format("Unknown downstream protocol {}",
                                      static_cast<int>(downstream_protocol_)));
  };
  EXPECT_EQ(2L, TestUtility::findCounter(stats, stat_name)->value());
}

// Verify that by default headers with underscores in their names remain in both requests and
// responses when allowed in configuration.
TEST_P(ProtocolIntegrationTest, HeadersWithUnderscoresRemainByDefault) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"foo_bar", "baz"}});
  waitForNextUpstreamRequest();

  EXPECT_THAT(upstream_request_->headers(), ContainsHeader("foo_bar", "baz"));
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"bar_baz", "fooz"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(response->headers(), ContainsHeader("bar_baz", "fooz"));
}

// Verify that request with headers containing underscores is rejected when configured.
TEST_P(DownstreamProtocolIntegrationTest, HeadersWithUnderscoresCauseRequestRejected) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_common_http_protocol_options()->set_headers_with_underscores_action(
            envoy::config::core::v3::HttpProtocolOptions::REJECT_REQUEST);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"foo_bar", "baz"}});

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
    ASSERT_TRUE(response->reset());
    EXPECT_EQ((downstream_protocol_ == Http::CodecType::HTTP3
                   ? Http::StreamResetReason::ProtocolError
                   : Http::StreamResetReason::RemoteReset),
              response->resetReason());
  }
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("unexpected_underscore"));
}

// Verify that request with trailers containing underscores is rejected when configured.
TEST_P(DownstreamProtocolIntegrationTest, TrailerWithUnderscoresCauseRequestRejected) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_common_http_protocol_options()->set_headers_with_underscores_action(
            envoy::config::core::v3::HttpProtocolOptions::REJECT_REQUEST);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendTrailers(
      *request_encoder_,
      Http::TestRequestTrailerMapImpl{{"trailer1", "value1"}, {"trailer_2", "value2"}});

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
    ASSERT_TRUE(response->reset());
    EXPECT_EQ((downstream_protocol_ == Http::CodecType::HTTP3
                   ? Http::StreamResetReason::ProtocolError
                   : Http::StreamResetReason::RemoteReset),
              response->resetReason());
  }
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("unexpected_underscore"));
}

// Verify that headers with underscores in response do not cause request to be rejected.
TEST_P(ProtocolIntegrationTest, HeadersWithUnderscoresInResponseAllowRequest) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_common_http_protocol_options()->set_headers_with_underscores_action(
            envoy::config::core::v3::HttpProtocolOptions::REJECT_REQUEST);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"}});
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"bar_baz", "fooz"}}, false);
  upstream_request_->encodeData("b", false);
  upstream_request_->encodeTrailers(
      Http::TestResponseTrailerMapImpl{{"trailer1", "value1"}, {"response_trailer", "ok"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Both response headers and trailers must retain headers with underscores.
  EXPECT_THAT(response->headers(), ContainsHeader("bar_baz", "fooz"));
  EXPECT_THAT(*response->trailers(), ContainsHeader("response_trailer", "ok"));
}

TEST_P(DownstreamProtocolIntegrationTest, ValidZeroLengthContent) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"content-length", "0"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test we're following https://tools.ietf.org/html/rfc7230#section-3.3.2
// as best we can.
TEST_P(ProtocolIntegrationTest, 304WithBody) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "304"}, {"content-length", "2"}};
  ASSERT(upstream_request_ != nullptr);
  upstream_request_->encodeHeaders(response_headers, false);
  response->waitForHeaders();
  EXPECT_EQ("304", response->headers().getStatusValue());

  // For HTTP/1.1 http_parser is explicitly told that 304s are header-only
  // requests.
  if (downstream_protocol_ == Http::CodecType::HTTP1 ||
      upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
  } else {
    ASSERT_FALSE(response->complete());
  }

  // Note that at this point the parser is expecting a new response, and it
  // signals an error because it cannot parse the received data as a valid
  // status line.
  upstream_request_->encodeData("aa\r\n", true);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // Any body sent after the request is considered complete will not be handled as part of the
    // active request, but will be flagged as a protocol error for the no-longer-associated
    // connection.
    // Ideally if we got the body with the headers we would instead reset the
    // stream, but it turns out that's complicated so instead we consistently
    // forward the headers and error out after.
    test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_protocol_error", 1);
  }

  // Only for HTTP/2 and Http/3, where streams are ended with an explicit end-stream so we
  // can differentiate between 304-with-advertised-but-absent-body and
  // 304-with-body, is there a protocol error on the active stream.
  if (downstream_protocol_ != Http::CodecType::HTTP1 &&
      upstreamProtocol() != Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->waitForReset());
  }
}

TEST_P(ProtocolIntegrationTest, OverflowingResponseCode) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // The http1 codec won't send illegal status codes so send raw HTTP/1.1
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    ASSERT(fake_upstream_connection != nullptr);
    ASSERT_TRUE(fake_upstream_connection->write(
        "HTTP/1.1 11111111111111111111111111111111111111111111111111111111111111111 OK\r\n",
        false));
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  } else {
    waitForNextUpstreamRequest();
    default_response_headers_.setStatus(
        "11111111111111111111111111111111111111111111111111111111111111111");
    upstream_request_->encodeHeaders(default_response_headers_, false);
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("protocol_error"));
}

TEST_P(ProtocolIntegrationTest, OverflowingResponseCodeStreamError) {
  // For H/1 this test is equivalent to OverflowingResponseCode
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  setUpstreamOverrideStreamErrorOnInvalidHttpMessage();
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  default_response_headers_.setStatus(
      "11111111111111111111111111111111111111111111111111111111111111111");
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // The upstream stream should be reset
  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("protocol_error"));
  // Upstream connection should stay up
  ASSERT_TRUE(fake_upstream_connection_->connected());
}

TEST_P(ProtocolIntegrationTest, MissingStatus) {
  initialize();

  // HTTP1, uses a defined protocol which doesn't split up messages into raw byte frames
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    ASSERT(fake_upstream_connection != nullptr);
    ASSERT_TRUE(fake_upstream_connection->write("HTTP/1.1 OK\r\n", false));
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  } else if (upstreamProtocol() == Http::CodecType::HTTP2) {
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    Http::Http2::Http2Frame::SettingsFlags settings_flags =
        static_cast<Http::Http2::Http2Frame::SettingsFlags>(0);
    Http2Frame setting_frame = Http::Http2::Http2Frame::makeEmptySettingsFrame(settings_flags);
    // Ack settings
    settings_flags = static_cast<Http::Http2::Http2Frame::SettingsFlags>(1); // ack
    Http2Frame ack_frame = Http::Http2::Http2Frame::makeEmptySettingsFrame(settings_flags);
    ASSERT(fake_upstream_connection->write(std::string(setting_frame))); // empty settings
    ASSERT(fake_upstream_connection->write(std::string(ack_frame)));     // ack setting
    Http::Http2::Http2Frame missing_status = Http::Http2::Http2Frame::makeHeadersFrameNoStatus(1);
    ASSERT_TRUE(fake_upstream_connection->write(std::string(missing_status)));
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  } else {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    waitForNextUpstreamRequest();
    default_response_headers_.removeStatus();
    upstream_request_->encodeHeaders(default_response_headers_, false);
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
}

TEST_P(ProtocolIntegrationTest, MissingStatusStreamError) {
  // For H/1 this test is equivalent to MissingStatus
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  setUpstreamOverrideStreamErrorOnInvalidHttpMessage();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  waitForNextUpstreamRequest();
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    Http::Http2::Http2Frame missing_status = Http::Http2::Http2Frame::makeHeadersFrameNoStatus(1);
    ASSERT_TRUE(fake_upstream_connection_->postWriteRawData(std::string(missing_status)));
  } else {
    default_response_headers_.removeStatus();
    upstream_request_->encodeHeaders(default_response_headers_, false);
  }

  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("502", response->headers().getStatusValue());
}

// Validate that lots of tiny cookies doesn't cause a DoS (single cookie header).
TEST_P(DownstreamProtocolIntegrationTest, LargeCookieParsingConcatenated) {
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    // QUICHE Qpack splits concatenated cookies into crumbs to increase
    // compression ratio. On the receiver side, the total size of these crumbs
    // may be larger than coalesced cookie headers. Increase the max request
    // header size to avoid QUIC_HEADERS_TOO_LARGE stream error.
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          hcm.mutable_max_request_headers_kb()->set_value(96);
          hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(8000);
        });
  }
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    setMaxRequestHeadersKb(96);
    setMaxRequestHeadersCount(8000);
  }
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"content-length", "0"}};
  std::vector<std::string> cookie_pieces;
  cookie_pieces.reserve(7000);
  for (int i = 0; i < 7000; i++) {
    cookie_pieces.push_back(fmt::sprintf("a%x=b", i));
  }
  request_headers.addCopy("cookie", absl::StrJoin(cookie_pieces, "; "));
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Validate that lots of tiny cookies doesn't cause a DoS (many cookie headers).
TEST_P(DownstreamProtocolIntegrationTest, LargeCookieParsingMany) {
  // Set header count limit to 2010.
  uint32_t max_count = 2010;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(
            max_count);
      });
  setMaxRequestHeadersCount(max_count);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"content-length", "0"}};
  for (int i = 0; i < 2000; i++) {
    request_headers.addCopy("cookie", fmt::sprintf("a%x=b", i));
  }
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, InvalidContentLength) {
  disable_client_header_validation_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {":scheme", "http"},
                                                                 {"content-length", "-1"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(codec_client_->waitForDisconnect());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
    test_server_->waitForCounterGe("http.config_test.downstream_rq_4xx", 1);
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, InvalidContentLengthAllowed) {
  disable_client_header_validation_ = true;
  setDownstreamOverrideStreamErrorOnInvalidHttpMessage();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"content-length", "-1"}});
  auto response = std::move(encoder_decoder.second);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ((downstream_protocol_ == Http::CodecType::HTTP3
                   ? Http::StreamResetReason::ProtocolError
                   : Http::StreamResetReason::RemoteReset),
              response->resetReason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, MultipleContentLengths) {
  disable_client_header_validation_ = true;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {":scheme", "http"},
                                                                 {"content-length", "3,2"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(codec_client_->waitForDisconnect());

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }
}

// TODO(PiotrSikora): move this HTTP/2 only variant to http2_integration_test.cc.
TEST_P(DownstreamProtocolIntegrationTest, MultipleContentLengthsAllowed) {
  disable_client_header_validation_ = true;
  setDownstreamOverrideStreamErrorOnInvalidHttpMessage();
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {":scheme", "http"},
                                                                 {"content-length", "3,2"}});
  auto response = std::move(encoder_decoder.second);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ((downstream_protocol_ == Http::CodecType::HTTP3
                   ? Http::StreamResetReason::ProtocolError
                   : Http::StreamResetReason::RemoteReset),
              response->resetReason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, LocalReplyDuringEncoding) {
  config_helper_.prependFilter(R"EOF(
name: local-reply-during-encode
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"}});

  // Wait for the upstream request and begin sending a response with end_stream = false.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
  EXPECT_EQ(0, upstream_request_->body().length());
}

TEST_P(DownstreamProtocolIntegrationTest, LocalReplyDuringEncodingData) {
  config_helper_.prependFilter(R"EOF(
name: local-reply-during-encode-data
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for the upstream request and begin sending a response with end_stream = false.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  // Finish sending response headers before aborting the response.
  response->waitForHeaders();
  upstream_request_->encodeData(size_, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  // Response was aborted after headers were sent to the client.
  // The stream was reset. Client does not receive body or trailers.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0, response->body().length());
  EXPECT_EQ(nullptr, response->trailers());
}

TEST_P(DownstreamProtocolIntegrationTest, LargeRequestUrlRejected) {
  // Send one 95 kB URL with limit 60 kB headers.
  testLargeRequestUrl(95, 60);
}

TEST_P(DownstreamProtocolIntegrationTest, LargeRequestUrlAccepted) {
  // Send one 95 kB URL with limit 96 kB headers.
  testLargeRequestUrl(95, 96);
}

TEST_P(DownstreamProtocolIntegrationTest, LargeRequestHeadersRejected) {
  // Send one 95 kB header with limit 60 kB and 100 headers.
  testLargeRequestHeaders(95, 1, 60, 100);
}

TEST_P(DownstreamProtocolIntegrationTest, VeryLargeRequestHeadersRejected) {
  // Send one very large 600 kB header with limit 500 kB and 100 headers.
  // The limit and the header size are set in such a way to accommodate for flow control limits.
  // If the headers are too large and the flow control blocks the response is truncated and the test
  // flakes.
  testLargeRequestHeaders(600, 1, 500, 100);
}

TEST_P(DownstreamProtocolIntegrationTest, LargeRequestHeadersAccepted) {
  // Send one 100 kB header with limit 8192 kB (8 MB) and 100 headers.
  testLargeRequestHeaders(100, 1, 8192, 100);
}

TEST_P(ProtocolIntegrationTest, ManyLargeRequestHeadersAccepted) {
  // Send 70 headers each of size 100 kB with limit 8192 kB (8 MB) and 100 headers.
  testLargeRequestHeaders(100, 70, 8192, 100, TestUtility::DefaultTimeout);
}

namespace {
uint32_t adjustMaxSingleHeaderSizeForCodecLimits(uint32_t size,
                                                 const HttpProtocolTestParams& params) {
  if (params.http2_implementation == Http2Impl::Nghttp2 &&
      (params.downstream_protocol == Http::CodecType::HTTP2 ||
       params.upstream_protocol == Http::CodecType::HTTP2)) {
    // nghttp2 has a hard-coded, unconfigurable limit of 64k for a header in it's header
    // decompressor, so this test will always fail when using that codec.
    // Reduce the size so that it can pass and receive some test coverage.
    return 100;
  } else if (params.downstream_protocol == Http::CodecType::HTTP3 ||
             params.upstream_protocol == Http::CodecType::HTTP3) {
    // QUICHE has a hard-coded limit of 1024KiB in it's QPACK decoder.
    // Reduce the size so that it can pass and receive some test coverage.
    return 1023;
  }

  return size;
}
} // namespace

// Test a single header of the maximum allowed size.
TEST_P(ProtocolIntegrationTest, VeryLargeRequestHeadersAccepted) {
  uint32_t size = adjustMaxSingleHeaderSizeForCodecLimits(8191, GetParam());

  testLargeRequestHeaders(size, 1, 8192, 100, TestUtility::DefaultTimeout);
}

// Test a single header of the maximum allowed size.
TEST_P(ProtocolIntegrationTest, ManyLargeResponseHeadersAccepted) {
  // Send 70 headers each of size 100 kB with limit 8192 kB (8 MB) and 100 headers.
  testLargeResponseHeaders(100, 70, 8192, 100, TestUtility::DefaultTimeout);
}

// Test a single header of the maximum allowed size.
TEST_P(ProtocolIntegrationTest, VeryLargeResponseHeadersAccepted) {
  uint32_t size = adjustMaxSingleHeaderSizeForCodecLimits(8191, GetParam());

  testLargeResponseHeaders(size, 1, 8192, 100, TestUtility::DefaultTimeout);
}

TEST_P(DownstreamProtocolIntegrationTest, ManyRequestHeadersRejected) {
  // Send 101 empty headers with limit 60 kB and 100 headers.
  testLargeRequestHeaders(0, 101, 60, 80);
}

TEST_P(DownstreamProtocolIntegrationTest, ManyRequestHeadersAccepted) {
  // Send 145 empty headers with limit 60 kB and 150 headers.
  testLargeRequestHeaders(0, 140, 60, 150);
}

TEST_P(DownstreamProtocolIntegrationTest, ManyRequestTrailersRejected) {
  // Default header (and trailer) count limit is 100.
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  Http::TestRequestTrailerMapImpl request_trailers;
  for (int i = 0; i < 150; i++) {
    request_trailers.addCopy("trailer", std::string(1, 'a'));
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("431", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
  }
}

TEST_P(DownstreamProtocolIntegrationTest, ManyRequestTrailersAccepted) {
  // Set header (and trailer) count limit to 200.
  uint32_t max_count = 200;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(
            max_count);
      });
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  setMaxRequestHeadersCount(max_count);
  Http::TestRequestTrailerMapImpl request_trailers;
  for (int i = 0; i < 150; i++) {
    request_trailers.addCopy("trailer", std::string(1, 'a'));
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// This test uses an Http::HeaderMapImpl instead of an Http::TestHeaderMapImpl to avoid
// time-consuming byte size validations that will cause this test to timeout.
TEST_P(DownstreamProtocolIntegrationTest, ManyRequestHeadersTimeout) {
  // Set timeout for 5 seconds, and ensure that a request with 10k+ headers can be sent.
  testManyRequestHeaders(TestUtility::DefaultTimeout * 5);
}

TEST_P(DownstreamProtocolIntegrationTest, LargeRequestTrailersAccepted) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  testLargeRequestTrailers(60, 96);
}

TEST_P(DownstreamProtocolIntegrationTest, LargeRequestTrailersRejected) {
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  testLargeRequestTrailers(66, 60);
}

// This test uses an Http::HeaderMapImpl instead of an Http::TestHeaderMapImpl to avoid
// time-consuming byte size verification that will cause this test to timeout.
TEST_P(DownstreamProtocolIntegrationTest, ManyTrailerHeaders) {
  setMaxRequestHeadersKb(200);
  setMaxRequestHeadersCount(20005);

  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_max_request_headers_kb()->set_value(upstreamConfig().max_request_headers_kb_);
        hcm.mutable_common_http_protocol_options()->mutable_max_headers_count()->set_value(
            upstreamConfig().max_request_headers_count_);
      });

  auto request_trailers = Http::RequestTrailerMapImpl::create();
  for (int i = 0; i < 20000; i++) {
    request_trailers->addCopy(Http::LowerCaseString(std::to_string(i)), "");
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendTrailers(*request_encoder_, *request_trailers);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Regression tests for CVE-2019-18801. We only validate the behavior of large
// :method request headers, since the case of other large headers is
// covered in the various testLargeRequest-based integration tests here.
//
// BalsaParser rejects large method strings
// by default, because it only accepts known methods from a hardcoded list.
// HTTP/2 and HTTP/3 codecs accept large methods. The table below describes the
// expected behaviors (in addition we should never see an ASSERT or ASAN failure
// trigger).
//
// Downstream proto  Upstream proto    Behavior expected
// ----------------------------------------------------------
// HTTP/2 or HTTP/3  HTTP/2 or HTTP/3  Success
// HTTP/2 or HTTP/3  HTTP/1            Envoy will forward; backend will reject
// HTTP/1            HTTP/2 or HTTP/3  Envoy will reject
// HTTP/1            HTTP/1            Envoy will reject
TEST_P(ProtocolIntegrationTest, LargeRequestMethod) {
  // There will be no upstream connections for HTTP/1 downstream, we need to
  // test the full mesh regardless.
  testing_upstream_intentionally_ = true;
  const std::string long_method = std::string(48 * 1024, 'a');
  const Http::TestRequestHeaderMapImpl request_headers{{":method", long_method},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    auto encoder_decoder = codec_client_->startRequest(request_headers);
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else if (upstreamProtocol() == Http::CodecType::HTTP1) {
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
  }
}

// Tests StopAllIterationAndBuffer. Verifies decode-headers-return-stop-all-filter calls decodeData
// once after iteration is resumed.
TEST_P(DownstreamProtocolIntegrationTest, TestDecodeHeadersReturnsStopAll) {
  config_helper_.prependFilter(R"EOF(
name: call-decodedata-once-filter
)EOF");
  config_helper_.prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  config_helper_.prependFilter(R"EOF(
name: passthrough-filter
)EOF");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with headers and data.
  changeHeadersForStopAllTests(default_request_headers_, false);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  // Sleeps for 1s in order to be consistent with testDecodeHeadersReturnsStopAllWatermark.
  absl::SleepFor(absl::Seconds(1));
  codec_client_->sendData(*request_encoder_, size_, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends a request with headers, data, and trailers.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  for (int i = 0; i < count_; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyUpStreamRequestAfterStopAllFilter();
}

// Tests StopAllIterationAndWatermark. decode-headers-return-stop-all-filter sets buffer
// limit to 100. Verifies data pause when limit is reached, and resume after iteration continues.
TEST_P(DownstreamProtocolIntegrationTest, TestDecodeHeadersReturnsStopAllWatermark) {
  // The StopAllIteration with async host resolution messes with the expectations of this test.
  async_lb_ = false;

  config_helper_.prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  config_helper_.prependFilter(R"EOF(
name: passthrough-filter
)EOF");

  // Sets initial stream window to min value to make the client sensitive to a low watermark.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_initial_stream_window_size()->set_value(
            ::Envoy::Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with headers and data.
  changeHeadersForStopAllTests(default_request_headers_, true);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  // Gives buffer 1s to react to buffer limit.
  absl::SleepFor(absl::Seconds(1));
  codec_client_->sendData(*request_encoder_, size_, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends a request with headers, data, and trailers.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  // Gives buffer 1s to react to buffer limit.
  absl::SleepFor(absl::Seconds(1));
  codec_client_->sendData(*request_encoder_, size_, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyUpStreamRequestAfterStopAllFilter();
}

// Test two filters that return StopAllIterationAndBuffer back-to-back.
TEST_P(DownstreamProtocolIntegrationTest, TestTwoFiltersDecodeHeadersReturnsStopAll) {
  config_helper_.prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  config_helper_.prependFilter(R"EOF(
name: decode-headers-return-stop-all-filter
)EOF");
  config_helper_.prependFilter(R"EOF(
name: passthrough-filter
)EOF");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends a request with headers and data.
  changeHeadersForStopAllTests(default_request_headers_, false);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  for (int i = 0; i < count_ - 1; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  codec_client_->sendData(*request_encoder_, size_, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_, upstream_request_->bodyLength());
  EXPECT_EQ(true, upstream_request_->complete());

  // Sends a request with headers, data, and trailers.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  response = std::move(encoder_decoder_2.second);
  for (int i = 0; i < count_; i++) {
    codec_client_->sendData(*request_encoder_, size_, false);
  }
  Http::TestRequestTrailerMapImpl request_trailers{{"trailer", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyUpStreamRequestAfterStopAllFilter();
}

// Tests encodeHeaders() returns StopAllIterationAndBuffer.
TEST_P(DownstreamProtocolIntegrationTest, TestEncodeHeadersReturnsStopAll) {
  config_helper_.prependFilter(R"EOF(
name: encode-headers-return-stop-all-filter
)EOF");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers, data and trailers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  changeHeadersForStopAllTests(default_response_headers_, false);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  for (int i = 0; i < count_ - 1; i++) {
    upstream_request_->encodeData(size_, false);
  }
  // Sleeps for 1s in order to be consistent with testEncodeHeadersReturnsStopAllWatermark.
  absl::SleepFor(absl::Seconds(1));
  upstream_request_->encodeData(size_, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Data is added in encodeData for all protocols, and encodeTrailers for HTTP/2 and above.
  int times_added = upstreamProtocol() == Http::CodecType::HTTP1 ? 1 : 2;
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * times_added, response->body().size());
}

// Tests encodeHeaders() returns StopAllIterationAndWatermark.
TEST_P(DownstreamProtocolIntegrationTest, TestEncodeHeadersReturnsStopAllWatermark) {
  config_helper_.prependFilter(R"EOF(
name: encode-headers-return-stop-all-filter
)EOF");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });

  // Sets initial stream window to min value to make the upstream sensitive to a low watermark.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_http2_protocol_options()->mutable_initial_stream_window_size()->set_value(
            ::Envoy::Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Upstream responds with headers, data and trailers.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();

  changeHeadersForStopAllTests(default_response_headers_, true);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  for (int i = 0; i < count_ - 1; i++) {
    upstream_request_->encodeData(size_, false);
  }
  // Gives buffer 1s to react to buffer limit.
  absl::SleepFor(absl::Seconds(1));
  upstream_request_->encodeData(size_, false);
  Http::TestResponseTrailerMapImpl response_trailers{{"response", "trailer"}};
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // Data is added in encodeData for all protocols, and encodeTrailers for HTTP/2 and above.
  int times_added = upstreamProtocol() == Http::CodecType::HTTP1 ? 1 : 2;
  EXPECT_EQ(count_ * size_ + added_decoded_data_size_ * times_added, response->body().size());
}

// Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
// combine set-cookie headers
TEST_P(ProtocolIntegrationTest, MultipleCookiesAndSetCookies) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},  {":path", "/dynamo/url"},
      {":scheme", "http"}, {":authority", "sni.lyft.com"},
      {"cookie", "a=b"},   {"cookie", "c=d"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"set-cookie", "foo"}, {"set-cookie", "bar"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Cookie)[0]->value(),
              "a=b; c=d");
  }

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  const auto out = response->headers().get(Http::LowerCaseString("set-cookie"));
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0]->value().getStringView(), "foo");
  ASSERT_EQ(out[1]->value().getStringView(), "bar");
}

// Test that delay closed connections are eventually force closed when the timeout triggers.
TEST_P(DownstreamProtocolIntegrationTest, TestDelayedConnectionTeardownTimeoutTrigger) {
  config_helper_.prependFilter("{ name: encoder-decoder-buffer-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        // 200ms.
        hcm.mutable_delayed_close_timeout()->set_nanos(200000000);
        hcm.mutable_drain_timeout()->set_seconds(1);
        hcm.mutable_common_http_protocol_options()->mutable_idle_timeout()->set_seconds(1);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  // The delayed close timeout should trigger since client is not closing the connection.
  EXPECT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(5000)));
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            1);
}

// Resets the downstream stream immediately and verifies that we clean up everything.
TEST_P(ProtocolIntegrationTest, TestDownstreamResetIdleTimeout) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.setDownstreamHttpIdleTimeout(std::chrono::milliseconds(100 * TIMEOUT_FACTOR));

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);

  EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    codec_client_->close();
  } else {
    codec_client_->sendReset(encoder_decoder.first);
  }

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_THAT(waitForAccessLog(access_log_name_), Not(HasSubstr("DPE")));
}

// Test that with http1_safe_max_connection_duration set to true, drain_timeout is not used for
// http1 connections after max_connection_duration is reached. Instead, envoy waits for the next
// request, adds connection:close to the response headers, then closes the connection after the
// stream ends.
TEST_P(ProtocolIntegrationTest, Http1SafeConnDurationTimeout) {
  config_helper_.setDownstreamMaxConnectionDuration(std::chrono::milliseconds(500));
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.mutable_drain_timeout()->set_nanos(1'000'000 /*=1ms*/);
        hcm.set_http1_safe_max_connection_duration(true);
      });
  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(10000)));
    test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
    EXPECT_EQ(test_server_->gauge("http.config_test.downstream_cx_http1_soft_drain")->value(), 0);
    // The rest of the test is only for http1.
    return;
  }

  // Wait until after the max connection duration
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
  test_server_->waitForGaugeGe("http.config_test.downstream_cx_http1_soft_drain", 1);

  // Envoy now waits for one more request/response over this connection before sending the
  // connection:close header and closing the connection. No matter how long the request/response
  // takes, envoy will not close the connection until it's able to send the connection:close header
  // downstream in a response.
  //
  // Sleeping for longer than the drain phase duration just to show it is no longer relevant.
  absl::SleepFor(absl::Seconds(1));

  auto soft_drain_response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(soft_drain_response->waitForEndStream());
  // Envoy will close the connection after the response has been sent.
  ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(10000)));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(soft_drain_response->complete());

  // The client must have been notified that the connection will be closed.
  EXPECT_EQ(soft_drain_response->headers().getConnectionValue(),
            Http::Headers::get().ConnectionValues.Close);
}

// Test connection is closed after single request processed.
TEST_P(ProtocolIntegrationTest, ConnDurationTimeoutBasic) {
  config_helper_.setDownstreamMaxConnectionDuration(std::chrono::milliseconds(500));
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_drain_timeout()->set_seconds(1); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(10000)));
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
}

// Test inflight request is processed correctly when timeout fires during request processing.
TEST_P(ProtocolIntegrationTest, ConnDurationInflightRequest) {
  config_helper_.setDownstreamMaxConnectionDuration(std::chrono::milliseconds(500));
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_drain_timeout()->set_seconds(1); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  // block and wait for counter to increase
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);

  // ensure request processed correctly
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_200", 1);

  ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(10000)));
}

// Test connection is closed if no http requests were processed
TEST_P(DownstreamProtocolIntegrationTest, ConnDurationTimeoutNoHttpRequest) {
  config_helper_.setDownstreamMaxConnectionDuration(std::chrono::milliseconds(500));
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_drain_timeout()->set_seconds(1); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(10000)));
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
}

TEST_P(ProtocolIntegrationTest, TestPreconnect) {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->mutable_preconnect_policy()->mutable_per_upstream_preconnect_ratio()->set_value(2.0);

    if (upstreamProtocol() == Http::CodecType::HTTP2) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()
          ->mutable_http2_protocol_options()
          ->mutable_max_concurrent_streams()
          ->set_value(4);
      ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    } else if (upstreamProtocol() == Http::CodecType::HTTP3) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()
          ->mutable_http3_protocol_options()
          ->mutable_quic_protocol_options()
          ->mutable_max_concurrent_streams()
          ->set_value(4);
      ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    }
  });
  autonomous_upstream_ = true;
  initialize();

  // First request should cause initial connections to be setup, enough for no more connections when
  // request concurrency doesn't exceed 1.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
  }

  // Preconnect is set to 2. Http 1 allows 1 request per connection so it requires 2 connections,
  // and http is configured for 4 so it requires only 1 connection.
  uint32_t expected_upstream_cx = (upstreamProtocol() == Http::CodecType::HTTP1) ? 2 : 1;
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", expected_upstream_cx);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", expected_upstream_cx);

  // Make several non-concurrent requests. The concurrency is only 1, so there should only be 1 or 2
  // upstream connections, already established by the first request.
  for (uint32_t i = 0; i < 10; i++) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
  }

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", expected_upstream_cx);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", expected_upstream_cx);

  if (GetParam().downstream_protocol == Http::CodecType::HTTP1) {
    // The rest of the test requires multiple concurrent requests and isn't written to use multiple
    // downstream connections.
    return;
  }

  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    // The http3 connection pool is currently establishing 3 connections, not the expected 5. It
    // appears that the accounting in http3 is different than in http1 or http2, but this
    // needs more investigation to understand if it's a bug, or just a difference.
    //
    // Previously, this test was only run for http1 upstreams, so this path was never tested.
    //
    // TODO(ggreenway): investigate and fix.
    return;
  }

  std::vector<std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr>> responses;

  // Create concurrent requests and validate that the connection counts are correct.
  constexpr uint32_t concurrent_requests = 10;
  for (uint32_t i = 0; i < concurrent_requests; i++) {
    // Create an outstanding request that doesn't complete.
    responses.push_back(codec_client_->startRequest(default_request_headers_));
  }

  expected_upstream_cx = (upstreamProtocol() == Http::CodecType::HTTP1)
                             ? (concurrent_requests * 2)
                             : (concurrent_requests * 2 / 4);

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", expected_upstream_cx);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", expected_upstream_cx);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", concurrent_requests);

  for (auto& response : responses) {
    codec_client_->sendData(response.first, 0, true);
    ASSERT_TRUE(response.second->waitForEndStream());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, BasicMaxStreamTimeout) {
  config_helper_.setDownstreamMaxStreamDuration(std::chrono::milliseconds(500));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  test_server_->waitForCounterGe("http.config_test.downstream_rq_max_duration_reached", 1);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("408", response->headers().getStatusValue());

  // We only wait for end stream, which means for example the QUIC STOP_SENDING
  // frame may be in-flight. Close the connection before response, which
  // receives callbacks, gets out of scope.
  codec_client_->close();
}

// Test that when request timeout and the request is completed, the gateway timeout (504) is
// returned as response code instead of request timeout (408).
TEST_P(ProtocolIntegrationTest, MaxStreamTimeoutWhenRequestIsNotComplete) {
  config_helper_.setDownstreamMaxStreamDuration(std::chrono::milliseconds(500));

  autonomous_upstream_ = false;

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The request is not header only. Envoy is expecting more data to end the request.
  auto encoder_decoder =
      codec_client_->startRequest(default_request_headers_, /*header_only_request=*/true);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  test_server_->waitForCounterGe("http.config_test.downstream_rq_max_duration_reached", 1);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, MaxRequestsPerConnectionReached) {
  config_helper_.setDownstreamMaxRequestsPerConnection(2);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sending first request and waiting to complete the response.
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_max_requests_reached")->value(),
            0);

  // Sending second request and waiting to complete the response.
  auto encoder_decoder_2 = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder_2.first;
  auto response_2 = std::move(encoder_decoder_2.second);
  codec_client_->sendData(*request_encoder_, 1, true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response_2->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_2->complete());
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_max_requests_reached")->value(),
            1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ(nullptr, response->headers().Connection());
    EXPECT_EQ("close", response_2->headers().getConnectionValue());
  } else {
    EXPECT_TRUE(codec_client_->sawGoAway());
  }
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// Test that onDrainTimeout allows current stream to finish before closing connection for http1.
TEST_P(DownstreamProtocolIntegrationTest, MaxRequestsPerConnectionVsMaxConnectionDuration) {
  config_helper_.setDownstreamMaxRequestsPerConnection(2);
  config_helper_.setDownstreamMaxConnectionDuration(std::chrono::milliseconds(500));
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_drain_timeout()->set_nanos(500'000'000 /*=500ms*/); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_max_requests_reached")->value(),
            0);

  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
  // http1 is not closed at this point because envoy needs to send a response with the
  // connection:close response header to be able to safely close the connection. For other protocols
  // it's safe for envoy to just close the connection, so they do so.
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    EXPECT_TRUE(codec_client_->sawGoAway());
    // The rest of the test is only for http1.
    return;
  }

  // Sending second request.
  auto response_2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Before sending the response, sleep past the drain timer. Nothing should happen.
  timeSystem().advanceTimeWait(Seconds(1));
  EXPECT_FALSE(codec_client_->sawGoAway());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  ASSERT_TRUE(response_2->waitForEndStream());
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response_2->complete());
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_max_requests_reached")->value(),
            1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_NE(nullptr, response_2->headers().Connection());
    EXPECT_EQ("close", response_2->headers().getConnectionValue());
  } else {
    EXPECT_TRUE(codec_client_->sawGoAway());
  }
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// Test that max stream duration still works after max requests per connection is reached (i.e. the
// final response is still time bounded). Also, if if max_stream_duration is triggered, it should
// add the connection:close header if the downstream protocol is http1 and this will be the last
// response!
TEST_P(DownstreamProtocolIntegrationTest, MaxRequestsPerConnectionVsMaxStreamDuration) {
  config_helper_.setDownstreamMaxRequestsPerConnection(2);
  config_helper_.setDownstreamMaxStreamDuration(std::chrono::milliseconds(500));

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sending first request and waiting to complete the response.
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_max_requests_reached")->value(),
            0);

  // Sending second request and waiting to complete the response.
  auto response_2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_max_requests_reached")->value(),
            1);

  // Don't send a response. HCM should sendLocalReply after max stream duration has elapsed.
  test_server_->waitForCounterGe("http.config_test.downstream_rq_max_duration_reached", 1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response_2->complete());
    // This will be the last request / response; envoy's going to close the connection after this
    // stream ends. We should notify the client.
    EXPECT_EQ("close", response_2->headers().getConnectionValue());
  } else {
    ASSERT_TRUE(response_2->waitForEndStream());
    codec_client_->close();
  }
}

// Make sure that invalid authority headers get blocked at or before the HCM.
TEST_P(DownstreamProtocolIntegrationTest, InvalidAuthority) {
  disable_client_header_validation_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "ho|st|"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    // For HTTP/1 this is handled by the HCM, which sends a full 400 response.
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    // For HTTP/2 this is handled by nghttp2 which resets the connection without
    // sending an HTTP response.
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_FALSE(response->complete());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, ConnectIsBlocked) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "CONNECT"}, {":authority", "sni.lyft.com.com:80"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Because CONNECT requests do not include a path, they will fail
  // to find a route match and return a 404.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_TRUE(response->complete());
  // Wait to process STOP_SENDING on the client for quic.
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_TRUE(response->waitForReset());
  }
}

// Make sure that with override_stream_error_on_invalid_http_message true, CONNECT
// results in stream teardown not connection teardown.
TEST_P(DownstreamProtocolIntegrationTest, ConnectStreamRejection) {
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  setDownstreamOverrideStreamErrorOnInvalidHttpMessage();

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":authority", "sni.lyft.com"}});

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().getStatusValue());
  EXPECT_FALSE(codec_client_->disconnected());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/12131
TEST_P(DownstreamProtocolIntegrationTest, Test100AndDisconnect) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  ASSERT_TRUE(fake_upstream_connection_->close());

  // Make sure that a disconnect results in valid 5xx response headers even when preceded by a 100.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, HeaderNormalizationRejection) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.set_path_with_escaped_slashes_action(
            envoy::extensions::filters::network::http_connection_manager::v3::
                HttpConnectionManager::REJECT_REQUEST);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/test/long%2Furl");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  EXPECT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Tests a filter that returns a FilterHeadersStatus::Continue after a local reply without
// processing new metadata generated in decodeHeader
TEST_P(DownstreamProtocolIntegrationTest, LocalReplyWithMetadata) {
  config_helper_.prependFilter(R"EOF(
  name: local-reply-with-metadata-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  ASSERT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ProtocolIntegrationTest, ContinueAllFromDecodeMetadata) {
  if (downstream_protocol_ == Http::CodecType::HTTP1 ||
      upstreamProtocol() == Http::CodecType::HTTP1) {
    GTEST_SKIP() << "Metadata is not enabled for HTTP1 protocols.";
  }

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    if (upstreamProtocol() == Http::CodecType::HTTP3) {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http3_protocol_options()
          ->set_allow_metadata(true);
    } else {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http2_protocol_options()
          ->set_allow_metadata(true);
    }
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  config_helper_.prependFilter(R"EOF(
  name: metadata-control-filter
  )EOF");
  autonomous_upstream_ = false;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  Http::RequestEncoder& encoder = encoder_decoder.first;
  IntegrationStreamDecoderPtr& decoder = encoder_decoder.second;
  // Allow the headers through.
  Http::MetadataMap metadata;
  metadata["should_continue"] = "true";
  codec_client_->sendMetadata(encoder, metadata);

  waitForNextUpstreamConnection({0}, std::chrono::milliseconds(500), fake_upstream_connection_);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  codec_client_->sendData(encoder, "abc", false);
  codec_client_->sendMetadata(encoder, metadata);

  codec_client_->sendData(encoder, "", true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(decoder->waitForEndStream(std::chrono::milliseconds(500)));
  EXPECT_EQ("200", decoder->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

TEST_P(DownstreamProtocolIntegrationTest, ContinueAllFromDecodeMetadataFollowedByLocalReply) {
  if (downstream_protocol_ == Http::CodecType::HTTP1 ||
      upstreamProtocol() == Http::CodecType::HTTP1) {
    GTEST_SKIP() << "Metadata is not enabled for HTTP1 protocols.";
  }

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    if (upstreamProtocol() == Http::CodecType::HTTP3) {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http3_protocol_options()
          ->set_allow_metadata(true);
    } else {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http2_protocol_options()
          ->set_allow_metadata(true);
    }
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });
  config_helper_.prependFilter(R"EOF(
  name: local-reply-during-decode
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: metadata-control-filter
  )EOF");
  autonomous_upstream_ = false;
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  Http::RequestEncoder& encoder = encoder_decoder.first;
  IntegrationStreamDecoderPtr& decoder = encoder_decoder.second;
  // Send some data; this should be buffered.
  codec_client_->sendData(encoder, "abc", false);
  // Allow the headers through.
  Http::MetadataMap metadata;
  metadata["should_continue"] = "true";
  codec_client_->sendMetadata(encoder, metadata);

  ASSERT_TRUE(decoder->waitForEndStream(std::chrono::milliseconds(500)));
  EXPECT_EQ("500", decoder->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

TEST_P(ProtocolIntegrationTest, ContinueAllFromEncodeMetadata) {
  if (downstream_protocol_ == Http::CodecType::HTTP1 ||
      upstreamProtocol() == Http::CodecType::HTTP1) {
    GTEST_SKIP() << "Metadata is not enabled for HTTP1 protocols.";
  }
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    ConfigHelper::HttpProtocolOptions protocol_options;
    if (upstreamProtocol() == Http::CodecType::HTTP3) {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http3_protocol_options()
          ->set_allow_metadata(true);
    } else {
      protocol_options.mutable_explicit_http_config()
          ->mutable_http2_protocol_options()
          ->set_allow_metadata(true);
    }
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  config_helper_.prependFilter(R"EOF(
  name: metadata-control-filter
  )EOF");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
  autonomous_upstream_ = false;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  Http::MetadataMap metadata_map;
  metadata_map["should_continue"] = "true";
  auto metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));

  upstream_request_->encodeMetadata(metadata_map_vector);
  response->waitForHeaders();
  upstream_request_->encodeData("", true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verify that host's trailing dot is removed and matches the domain for routing request.
TEST_P(ProtocolIntegrationTest, EnableStripTrailingHostDot) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.set_strip_trailing_host_dot(true);
        // clear existing domains and add new domain.
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        virtual_host->clear_domains();
        virtual_host->add_domains("sni.lyft.com");
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com."}});
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verify that host's trailing dot is not removed and thus fails to match configured domains for
// routing request.
TEST_P(DownstreamProtocolIntegrationTest, DisableStripTrailingHostDot) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.set_strip_trailing_host_dot(false);
        // clear existing domains and add new domain.
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        virtual_host->clear_domains();
        virtual_host->add_domains("sni.lyft.com");
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com."}});
  // Expect local reply as request host fails to match configured domains.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

static std::string remove_response_headers_filter = R"EOF(
name: remove-response-headers-filter
)EOF";

TEST_P(ProtocolIntegrationTest, HeadersOnlyRequestWithRemoveResponseHeadersFilter) {
  config_helper_.prependFilter(remove_response_headers_filter);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(response->waitForEndStream());
  // If a filter chain removes :status from the response headers, then Envoy must reply with
  // BadGateway and must not crash.
  ASSERT_TRUE(codec_client_->connected());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), HasSubstr("missing required header: :status"));
}

TEST_P(ProtocolIntegrationTest, RemoveResponseHeadersFilter) {
  config_helper_.prependFilter(remove_response_headers_filter);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, 10);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(response->waitForEndStream());
  // If a filter chain removes :status from the response headers, then Envoy must reply with
  // BadGateway and not crash.
  ASSERT_TRUE(codec_client_->connected());
  EXPECT_EQ("502", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), HasSubstr("missing required header: :status"));
}

TEST_P(ProtocolIntegrationTest, ReqRespSizeStats) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.mutable_track_cluster_stats()->set_request_response_sizes(true);
  });
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/found"}, {":scheme", "http"}, {":authority", "foo.lyft.com"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0, 0,
                                                TestUtility::DefaultTimeout);
  EXPECT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rq_headers_size");
  test_server_->waitUntilHistogramHasSamples("cluster.cluster_0.upstream_rs_headers_size");
}

// Verify that when a filter encodeHeaders callback overflows response buffer in filter manager the
// filter chain is aborted and 500 is sent to the client.
TEST_P(ProtocolIntegrationTest, OverflowEncoderBufferFromEncodeHeaders) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: ENCODE_HEADERS
      body_size: 70000
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: false
      crash_in_encode_data: false
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, response_headers, 10,
                                                0, TestUtility::DefaultTimeout);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that when a filter encodeData callback overflows response buffer in filter manager the
// filter chain is aborted and 500 is sent to the client in case where upstream response headers
// have not yet been sent.
TEST_P(ProtocolIntegrationTest, OverflowEncoderBufferFromEncodeDataWithResponseHeadersUnsent) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  // Buffer filter will stop iteration from encodeHeaders preventing response headers from being
  // sent downstream.
  config_helper_.prependFilter(R"EOF(
  name: encoder-decoder-buffer-filter
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: true
      crash_in_encode_data: true
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  // This much data should overflow the 64Kb response buffer.
  upstream_request_->encodeData(16 * 1024, false);
  upstream_request_->encodeData(64 * 1024, false);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that when a filter encodeData callback overflows response buffer in filter manager the
// filter chain is aborted and stream is reset in case where upstream response headers have already
// been sent.
TEST_P(ProtocolIntegrationTest, OverflowEncoderBufferFromEncodeData) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  // Make the add-body-filter stop iteration from encodeData. Headers should be sent to the client.
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: ENCODE_DATA
      where_to_stop_and_buffer: ENCODE_DATA
      body_size: 16384
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_encode_headers: false
      crash_in_encode_data: true
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  // Finish sending response headers before aborting the response.
  response->waitForHeaders();
  // This much data should cause the add-body-filter to overflow response buffer
  upstream_request_->encodeData(16 * 1024, false);
  upstream_request_->encodeData(64 * 1024, false);
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verify that when a filter decodeHeaders callback overflows request buffer in filter manager the
// filter chain is aborted and 413 is sent to the client.
TEST_P(DownstreamProtocolIntegrationTest, OverflowDecoderBufferFromDecodeHeaders) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: true
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_HEADERS
      body_size: 70000
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// Verify that when a filter decodeData callback overflows request buffer in filter manager the
// filter chain is aborted and 413 is sent to the client.
TEST_P(DownstreamProtocolIntegrationTest, OverflowDecoderBufferFromDecodeData) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: true
      crash_in_decode_data: true
  )EOF");
  // Buffer filter causes filter manager to buffer data
  config_helper_.prependFilter(R"EOF(
  name: encoder-decoder-buffer-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // This much data should overflow request buffer in filter manager
  codec_client_->sendData(*request_encoder, 16 * 1024, false);
  codec_client_->sendData(*request_encoder, 64 * 1024, false);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

// Verify that when a filter decodeData callback overflows request buffer in filter manager the
// filter chain is aborted and 413 is sent to the client. In this test the overflow occurs after
// filter chain iteration was restarted. It is very similar to the test case above but some filter
// manager's internal state is slightly different.
TEST_P(DownstreamProtocolIntegrationTest, OverflowDecoderBufferFromDecodeDataContinueIteration) {
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: false
      crash_in_decode_data: true
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_DATA
      body_size: 70000
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: encoder-decoder-buffer-filter
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // This should cause some data to be buffered without overflowing request buffer.
  codec_client_->sendData(*request_encoder, 16 * 1024, false);
  // The buffer filter will resume filter chain iteration and the next add-body-filter filter
  // will overflow the request buffer.
  codec_client_->sendData(*request_encoder, 16 * 1024, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// Adding data in decodeTrailers without any data in the filter manager's request buffer should work
// as it will overflow the pending_recv_data_ which will cause downstream window updates to stop.
TEST_P(DownstreamProtocolIntegrationTest,
       OverflowDecoderBufferFromDecodeTrailersWithContinuedIteration) {
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_TRAILERS
      body_size: 70000
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder, 1024, false);
  codec_client_->sendData(*request_encoder, 1024, false);

  codec_client_->sendTrailers(*request_encoder,
                              Http::TestRequestTrailerMapImpl{{"some", "trailer"}});

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Adding data in decodeTrailers with some data in the filter manager's request buffer should case
// 413 as it will overflow the request buffer in filter manager.
TEST_P(DownstreamProtocolIntegrationTest, OverflowDecoderBufferFromDecodeTrailers) {
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  config_helper_.setBufferLimits(64 * 1024, 64 * 1024);
  config_helper_.prependFilter(R"EOF(
  name: crash-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.CrashFilterConfig
      crash_in_decode_headers: false
      crash_in_decode_data: true
      crash_in_decode_trailers: true
  )EOF");
  config_helper_.prependFilter(R"EOF(
  name: add-body-filter
  typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      where_to_add_body: DECODE_TRAILERS
      where_to_stop_and_buffer: DECODE_DATA
      body_size: 70000
  )EOF");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder, 1024, false);
  codec_client_->sendData(*request_encoder, 1024, false);

  codec_client_->sendTrailers(*request_encoder,
                              Http::TestRequestTrailerMapImpl{{"some", "trailer"}});

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

// TODO(#26484): Re-enable test after resolving flake.
TEST_P(
    ProtocolIntegrationTest,
    DISABLED_EnvoyUpstreamResetAfterFullResponseReceivedAndRequestFullyReceivedButNotEntirelySerialized) {
  // No reset for HTTP1.
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  // QUIC does not yet populate downstream_cx_rx_bytes_total.
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  constexpr uint32_t window_size = 65535;
  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())
          .value();
  http2_options.mutable_initial_stream_window_size()->set_value(window_size);
  http2_options.mutable_initial_connection_window_size()->set_value(window_size);

  envoy::config::listener::v3::QuicProtocolOptions quic_options;
  auto* quic_protocol_options = quic_options.mutable_quic_protocol_options();
  quic_protocol_options->mutable_initial_connection_window_size()->set_value(window_size);
  quic_protocol_options->mutable_initial_stream_window_size()->set_value(window_size);

  // Make the fake upstreams use a small window size.
  mergeOptions(http2_options);
  mergeOptions(quic_options);

  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

  // The response is larger than the stream flow control window. So it will
  // remain in the Envoy's upstream codec waiting for serialization when given
  // more window.
  constexpr uint32_t request_size = 3 * window_size;

  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-length", absl::StrCat(request_size)}});
  auto& encoder = encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Don't grant additional window to the Envoy upstream codec.
  upstream_request_->readDisable(true);

  // Finalize the request from the downstream before the response has started.
  // The request will go through all of Envoy's stream layer, but not be
  // serialized on the wire due to flow control.
  codec_client_->sendData(encoder, request_size, true);
  test_server_->waitForCounterGe("http.config_test.downstream_cx_rx_bytes_total", request_size);

  // Now that the downstream request has fully made it to Envoy, encode the response.
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
      },
      true);

  auto response = std::move(encoder_decoder.second);
  response->waitForHeaders();

  // HCM thinks we have no active stream now.
  test_server_->waitForCounterEq("http.config_test.downstream_rq_completed", 1);
  test_server_->waitForGaugeEq("http.config_test.downstream_rq_active", 0);

  // There is no reset for the cluster yet.
  if (upstreamProtocol() == Envoy::Http::CodecType::HTTP2) {
    test_server_->waitForCounterEq("cluster.cluster_0.http2.rx_reset", 0);
  } else {
    test_server_->waitForCounterEq("cluster.cluster_0.http3.rx_reset", 0);
  }

  // Send the reset stream to Envoy's upstream codec client. Envoy protocol
  // agnostic stream representations should already be destroyed but the codec
  // should still know about the stream.
  upstream_request_->encodeResetStream();

  if (upstreamProtocol() == Envoy::Http::CodecType::HTTP2) {
    test_server_->waitForCounterEq("cluster.cluster_0.http2.rx_reset", 1);
  } else {
    test_server_->waitForCounterEq("cluster.cluster_0.http3.rx_reset", 1);
  }
}

TEST_P(ProtocolIntegrationTest, ResetLargeResponseUponReceivingHeaders) {
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())
          .value();
  http2_options.mutable_initial_stream_window_size()->set_value(65535);
  http2_options.mutable_initial_connection_window_size()->set_value(65535);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

  // The response is larger than the stream flow control window. So the reset of it will
  // likely be buffered in the QUIC stream send buffer.
  constexpr uint64_t response_size = 100 * 1024;

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"content-length", "10"}});
  auto& encoder = encoder_decoder.first;

  std::string data(10, 'a');
  codec_client_->sendData(encoder, data, true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-length", absl::StrCat(response_size)}},
      false);

  auto response = std::move(encoder_decoder.second);
  response->waitForHeaders();
  encoder.getStream().readDisable(true);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(fake_upstream_connection_->waitForNoPost());
  // Reset stream while the quic server stream has FIN buffered in its send buffer.
  codec_client_->sendReset(encoder);
  codec_client_->close();
}

TEST_P(ProtocolIntegrationTest, HeaderOnlyBytesCountUpstream) {
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
  testRouterRequestAndResponseWithBody(0, 0, false);
  const size_t wire_bytes_received =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 10 : 13;
  expectUpstreamBytesSentAndReceived(
      BytesCountExpectation(167, 38, 136, 18),
      BytesCountExpectation(120, wire_bytes_received, 120, wire_bytes_received),
      BytesCountExpectation(116, 5, 116, 3));
}

TEST_P(ProtocolIntegrationTest, HeaderOnlyBytesCountDownstream) {
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  testRouterRequestAndResponseWithBody(0, 0, false);
  expectDownstreamBytesSentAndReceived(BytesCountExpectation(124, 51, 105, 19),
                                       BytesCountExpectation(68, 34, 68, 34),
                                       BytesCountExpectation(8, 10, 8, 6));
}

TEST_P(ProtocolIntegrationTest, HeaderAndBodyWireBytesCountUpstream) {
  // we only care about upstream protocol.
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
  testRouterRequestAndResponseWithBody(100, 100, false);
  const size_t header_bytes_received =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 10 : 13;
  expectUpstreamBytesSentAndReceived(BytesCountExpectation(306, 158, 164, 27),
                                     BytesCountExpectation(229, 122, 120, header_bytes_received),
                                     BytesCountExpectation(219, 109, 116, 3));
}

TEST_P(ProtocolIntegrationTest, HeaderAndBodyWireBytesCountDownstream) {
  // we only care about upstream protocol.
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  testRouterRequestAndResponseWithBody(100, 100, false);
  expectDownstreamBytesSentAndReceived(BytesCountExpectation(244, 190, 114, 46),
                                       BytesCountExpectation(177, 173, 68, 34),
                                       BytesCountExpectation(111, 113, 8, 6));
}

TEST_P(ProtocolIntegrationTest, HeaderAndBodyWireBytesCountReuseDownstream) {
  // We only care about the downstream protocol.
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }

  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const int request_size = 100;
  const int response_size = 100;

  // Send first request on the connection
  auto response_one = sendRequestAndWaitForResponse(default_request_headers_, request_size,
                                                    default_response_headers_, response_size, 0);
  checkSimpleRequestSuccess(request_size, response_size, response_one.get());
  expectDownstreamBytesSentAndReceived(BytesCountExpectation(244, 190, 114, 46),
                                       BytesCountExpectation(177, 137, 68, 34),
                                       BytesCountExpectation(111, 137, 8, 6), 0);

  // Reuse connection, send the second request on the connection.
  auto response_two = sendRequestAndWaitForResponse(default_request_headers_, request_size,
                                                    default_response_headers_, response_size, 0);
  checkSimpleRequestSuccess(request_size, response_size, response_two.get());
  expectDownstreamBytesSentAndReceived(BytesCountExpectation(244, 190, 114, 46),
                                       BytesCountExpectation(148, 137, 15, 27),
                                       BytesCountExpectation(111, 137, 8, 6), 1);
}

TEST_P(ProtocolIntegrationTest, HeaderAndBodyWireBytesCountReuseUpstream) {
  // We only care about the upstream protocol.
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }

  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto second_client = makeHttpConnection(makeClientConnection(lookupPort("http")));
  const int request_size = 100;
  const int response_size = 100;

  // Send to the same upstream from the two clients.
  auto response_one = sendRequestAndWaitForResponse(default_request_headers_, request_size,
                                                    default_response_headers_, response_size, 0);
  const size_t http2_header_bytes_received =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 10 : 13;
  expectUpstreamBytesSentAndReceived(
      BytesCountExpectation(306, 158, 164, 27),
      BytesCountExpectation(223, 122, 120, http2_header_bytes_received),
      BytesCountExpectation(223, 108, 114, 3), 0);

  // Swap clients so the other connection is used to send the request.
  std::swap(codec_client_, second_client);
  auto response_two = sendRequestAndWaitForResponse(default_request_headers_, request_size,
                                                    default_response_headers_, response_size, 0);

  const size_t http2_header_bytes_sent =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 54 : 58;
  expectUpstreamBytesSentAndReceived(BytesCountExpectation(306, 158, 164, 27),
                                     BytesCountExpectation(167, 119, http2_header_bytes_sent, 10),
                                     BytesCountExpectation(114, 108, 11, 3), 1);
  second_client->close();
}

TEST_P(ProtocolIntegrationTest, TrailersWireBytesCountUpstream) {
  // we only care about upstream protocol.
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());

  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"response1", "trailer1"},
                                                     {"response2", "trailer2"}};
  initialize();

  uint64_t request_size = 10u;
  uint64_t response_size = 20u;
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, request_size, false);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  // The header compression instruction tables on both the client and Envoy need to be sync'ed with
  // request headers before sending trailers so that the compression of trailers can be
  // deterministic. To do so, wait for the body to be proxied to upstream before sending the
  // trailer, by which point Envoy likely has already sync'ed instruction table with the client.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, request_size));
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(response_size, false);
  // Wait for the body to be proxied to the client before sending trailers for the same reason.
  response->waitForBodyData(response_size);
  upstream_request_->encodeTrailers(response_trailers);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());
  EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(response_size, response->body().size());
  EXPECT_THAT(*response->trailers(), HeaderMapEqualRef(&response_trailers));

  const size_t http2_trailer_bytes_received =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 49 : 52;
  expectUpstreamBytesSentAndReceived(
      BytesCountExpectation(256, 120, 204, 67),
      BytesCountExpectation(181, 81, 162, http2_trailer_bytes_received),
      BytesCountExpectation(134, 33, 122, 7));
}

TEST_P(ProtocolIntegrationTest, TrailersWireBytesCountDownstream) {
  // we only care about upstream protocol.
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());

  testTrailers(10, 20, true, true);

  expectDownstreamBytesSentAndReceived(BytesCountExpectation(206, 140, 156, 84),
                                       BytesCountExpectation(136, 86, 107, 67),
                                       BytesCountExpectation(36, 26, 14, 10));
}

TEST_P(ProtocolIntegrationTest, DownstreamDisconnectBeforeRequestCompleteWireBytesCountUpstream) {
  // we only care about upstream protocol.
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");

  testRouterDownstreamDisconnectBeforeRequestComplete(nullptr);

  expectUpstreamBytesSentAndReceived(BytesCountExpectation(195, 0, 164, 0),
                                     BytesCountExpectation(120, 0, 120, 0),
                                     BytesCountExpectation(120, 0, 120, 0));
}

TEST_P(ProtocolIntegrationTest, DownstreamDisconnectBeforeRequestCompleteWireBytesCountDownstream) {
  // we only care about upstream protocol.
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");

  testRouterDownstreamDisconnectBeforeRequestComplete(nullptr);

  expectDownstreamBytesSentAndReceived(BytesCountExpectation(0, 79, 0, 46),
                                       BytesCountExpectation(0, 34, 0, 34),
                                       BytesCountExpectation(0, 8, 0, 6));
}

TEST_P(ProtocolIntegrationTest, UpstreamDisconnectBeforeRequestCompleteWireBytesCountUpstream) {
  // we only care about upstream protocol.
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");

  testRouterUpstreamDisconnectBeforeRequestComplete();

  expectUpstreamBytesSentAndReceived(BytesCountExpectation(195, 0, 164, 0),
                                     BytesCountExpectation(120, 0, 120, 0),
                                     BytesCountExpectation(120, 0, 120, 0));
}

TEST_P(ProtocolIntegrationTest, UpstreamDisconnectBeforeResponseCompleteWireBytesCountUpstream) {
  // we only care about upstream protocol.
  if (downstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
               "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");

  testRouterUpstreamDisconnectBeforeResponseComplete();

  const size_t http2_header_bytes_received =
      (GetParam().http2_implementation == Http2Impl::Oghttp2) ? 10 : 13;
  expectUpstreamBytesSentAndReceived(
      BytesCountExpectation(167, 47, 136, 27),
      BytesCountExpectation(120, http2_header_bytes_received, 120, http2_header_bytes_received),
      BytesCountExpectation(113, 5, 113, 3));
}

TEST_P(DownstreamProtocolIntegrationTest, BadRequest) {
  config_helper_.disableDelayClose();
  // we only care about upstream protocol.
  if (use_universal_header_validator_) {
    // permissive parsing is enabled
    return;
  }

  if (downstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  useAccessLog("%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED% "
               "%DOWNSTREAM_HEADER_BYTES_SENT% %DOWNSTREAM_HEADER_BYTES_RECEIVED%");
  initialize();
  std::string response;
  std::string full_request(100, '\r');
  full_request += "GET / HTTP/1.1\r\n path: /test/long/url\r\n"
                  "Host: host\r\ncontent-length: 0\r\n"
                  "transfer-encoding: chunked\r\n\r\n";

  sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, false);

  expectUpstreamBytesSentAndReceived(BytesCountExpectation(156, 200, 117, 0),
                                     BytesCountExpectation(113, 13, 113, 0),
                                     BytesCountExpectation(156, 200, 113, 0));
}

TEST_P(DownstreamProtocolIntegrationTest, PathWithFragmentRejectedByDefault) {
  // Prevent UHV in test client from stripping fragment
  disable_client_header_validation_ = true;
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  default_request_headers_.setPath("/some/path#fragment");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(ProtocolIntegrationTest, FragmentStrippedFromPathWithOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                    "false");
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  default_request_headers_.setPath("/some/path?p1=v1#fragment");
  Http::TestRequestHeaderMapImpl expected_request_headers{default_request_headers_};
  expected_request_headers.setPath("/some/path?p1=v1");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  auto response = sendRequestAndWaitForResponse(expected_request_headers, 0, response_headers, 0, 0,
                                                TestUtility::DefaultTimeout);
  EXPECT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, ValidateUpstreamHeaders) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    // For QUIC, even through the headers are not sent upstream, the stream will
    // be created. Use the autonomous upstream and allow incomplete streams.
    autonomous_allow_incomplete_streams_ = true;
    autonomous_upstream_ = true;
  }
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  config_helper_.prependFilter("{ name: invalid-header-filter, typed_config: { \"@type\": "
                               "type.googleapis.com/google.protobuf.Empty } }");

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-add-invalid-header-value", "true"}});
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  if (upstreamProtocol() != Http::CodecType::HTTP3) {
    EXPECT_THAT(waitForAccessLog(access_log_name_), testing::MatchesRegex(".*invalid.*value.*"));
  }
}

TEST_P(ProtocolIntegrationTest, ValidateUpstreamMixedCaseHeaders) {
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    autonomous_allow_incomplete_streams_ = true;
    autonomous_upstream_ = true;
  }
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    testing_upstream_intentionally_ = true;
  }
  config_helper_.prependFilter("{ name: invalid-header-filter, typed_config: { \"@type\": "
                               "type.googleapis.com/google.protobuf.Empty } }");

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Tell the invalid-header-filter to add a header with mixed case name.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-add-mixed-case-header-key", "true"}});

  // HTTP/1 upstream codec allows mixed case headers
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    waitForNextUpstreamRequest();

    // Note that the fake upstream will change the header name to lowercase
    EXPECT_EQ("some value here", upstream_request_->headers()
                                     .get(Http::LowerCaseString("x-mixed-case"))[0]
                                     ->value()
                                     .getStringView());

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("503", response->headers().getStatusValue());
  }
}

TEST_P(ProtocolIntegrationTest, ValidateUpstreamHeadersWithOverride) {
  if (use_universal_header_validator_) {
    // UHV always validated headers before sending them upstream. This test is not applicable
    // when UHV is enabled.
    return;
  }
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    testing_upstream_intentionally_ = true;
  }
  useAccessLog("%RESPONSE_CODE_DETAILS%");

  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.prependFilter("{ name: invalid-header-filter, typed_config: { \"@type\": "
                               "type.googleapis.com/google.protobuf.Empty } }");

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-add-invalid-header-key", "true"}});

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // HTTP/1 upstream will parse the invalid header as two values: x-foo and x-oops.
    // This is a defined and known behavior when the runtime guard is disabled.
    waitForNextUpstreamRequest();

    EXPECT_EQ("hello", upstream_request_->headers()
                           .get(Http::LowerCaseString("x-foo"))[0]
                           ->value()
                           .getStringView());
    EXPECT_EQ("yes", upstream_request_->headers()
                         .get(Http::LowerCaseString("x-oops"))[0]
                         ->value()
                         .getStringView());

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  } else if (upstreamProtocol() == Http::CodecType::HTTP2) {
    // nghttp2 throws an error when parsing the invalid header value, resets the
    // upstream connection, and sends back a local 503 reply.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

    response->waitForHeaders();

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    EXPECT_EQ("503", response->headers().getStatusValue());
    EXPECT_THAT(waitForAccessLog(access_log_name_),
                HasSubstr("upstream_reset_before_response_started{connection_termination}"));
  } else {
    response->waitForHeaders();

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    EXPECT_EQ("503", response->headers().getStatusValue());
  }
}

// Test buffering and then continuing after too many response bytes to buffer.
TEST_P(ProtocolIntegrationTest, BufferContinue) {
  // Bytes sent is configured for http/2 flow control windows.
  if (upstreamProtocol() != Http::CodecType::HTTP2) {
    return;
  }
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* header = virtual_host->mutable_response_headers_to_add()->Add()->mutable_header();
        header->set_key("foo");
        header->set_value("bar");
      });

  useAccessLog();
  config_helper_.addFilter("{ name: buffer-continue-filter }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl data("HTTP body content goes here");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  upstream_request_->encodeData(512, false);
  upstream_request_->encodeData(1024 * 100, false);

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("500", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, ContentLengthSmallerThanPayload) {
  if (use_universal_header_validator_) {
    // UHV does not track consistency of content-length and amount of DATA in HTTP/2
    return;
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-length", "123"}},
      1024);
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    waitForNextUpstreamRequest();
    // HTTP/1.x requests get the payload length from Content-Length header. The remaining bytes is
    // parsed as another request.
    EXPECT_EQ(123u, upstream_request_->body().length());
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_TRUE(response->complete());
  } else {
    // Inconsistency in content-length header and the actually body length should be treated as a
    // stream error.
    ASSERT_TRUE(response->waitForReset());
    EXPECT_EQ((downstreamProtocol() == Http::CodecType::HTTP3
                   ? Http::StreamResetReason::ProtocolError
                   : Http::StreamResetReason::RemoteReset),
              response->resetReason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, ContentLengthLargerThanPayload) {
  if (use_universal_header_validator_) {
    // UHV does not track consistency of content-length and amount of DATA in HTTP/2
    return;
  }

  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    // HTTP/1.x request rely on Content-Length header to determine payload length. So there is no
    // inconsistency but the request will hang there waiting for the rest bytes.
    return;
  }

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"content-length", "1025"}},
      1024);

  // Inconsistency in content-length header and the actually body length should be treated as a
  // stream error.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_EQ((downstreamProtocol() == Http::CodecType::HTTP3 ? Http::StreamResetReason::ProtocolError
                                                            : Http::StreamResetReason::RemoteReset),
            response->resetReason());
}

class NoUdpGso : public Api::OsSysCallsImpl {
public:
  bool supportsUdpGso() const override { return false; }
};

TEST_P(DownstreamProtocolIntegrationTest, HandleDownstreamSocketFail) {
  // Make sure for HTTP/3 Envoy will use sendmsg, so the write_matcher will work.
  NoUdpGso reject_gso_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&reject_gso_};
  ASSERT(!Api::OsSysCallsSingleton::get().supportsUdpGso());
  SocketInterfaceSwap socket_swap(downstreamProtocol() == Http::CodecType::HTTP3
                                      ? Network::Socket::Type::Datagram
                                      : Network::Socket::Type::Stream);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Makes us have Envoy's writes to downstream return EBADF
  Api::IoErrorPtr ebadf = Network::IoSocketError::getIoSocketEbadfError();
  socket_swap.write_matcher_->setSourcePort(lookupPort("http"));
  socket_swap.write_matcher_->setWriteOverride(std::move(ebadf));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    // For HTTP/3 since the packets are black holed, there is no client side
    // indication of connection close. Wait on Envoy stats instead.
    std::string counter_scope = version_ == Network::Address::IpVersion::v4
                                    ? "listener.127.0.0.1_0.http3.downstream.tx."
                                    : "listener.[__1]_0.http3.downstream.tx.";
    std::string error_code = "quic_connection_close_error_code_QUIC_PACKET_WRITE_ERROR";
    test_server_->waitForCounterEq(absl::StrCat(counter_scope, error_code), 1);
    codec_client_->close();
  } else {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
  socket_swap.write_matcher_->setWriteOverride(Api::IoError::none());
  // Shut down the server and upstreams before os_calls goes out of scope to avoid syscalls
  // during its removal.
  test_server_.reset();
  cleanupUpstreamAndDownstream();
  fake_upstreams_.clear();
}

TEST_P(ProtocolIntegrationTest, HandleUpstreamSocketFail) {
  SocketInterfaceSwap socket_swap(upstreamProtocol() == Http::CodecType::HTTP3
                                      ? Network::Socket::Type::Datagram
                                      : Network::Socket::Type::Stream);

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Make sure the headers made it through.
  waitForNextUpstreamConnection(std::vector<uint64_t>{0}, TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  AssertionResult result =
      fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());

  // Makes us have Envoy's writes to upstream return EBADF
  Api::IoErrorPtr ebadf = Network::IoSocketError::getIoSocketEbadfError();
  socket_swap.write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  socket_swap.write_matcher_->setWriteOverride(std::move(ebadf));

  Buffer::OwnedImpl data("HTTP body content goes here");
  codec_client_->sendData(*downstream_request, data, true);

  ASSERT_TRUE(response->waitForEndStream());
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_THAT(
        waitForAccessLog(access_log_name_),
        HasSubstr(
            "upstream_reset_before_response_started{connection_termination|QUIC_"
            "PACKET_WRITE_ERROR|FROM_SELF|Write_failed_with_error:_9_(Bad_file_descriptor)}"));
  } else {
    EXPECT_THAT(waitForAccessLog(access_log_name_),
                HasSubstr("upstream_reset_before_response_started{connection_termination}"));
  }
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  socket_swap.write_matcher_->setWriteOverride(Api::IoError::none());
  // Shut down the server before os_calls goes out of scope to avoid syscalls
  // during its removal.
  test_server_.reset();
  cleanupUpstreamAndDownstream();
}

// TODO(alyssawilk) fix windows build before flipping flag.
#ifndef WIN32
// A singleton which will fail creation of the Nth socket
class AllowForceFail : public Api::OsSysCallsImpl {
public:
  void startFailing() {
    absl::MutexLock m(mutex_);
    fail_ = true;
  }
  Api::SysCallSocketResult socket(int domain, int type, int protocol) override {
    absl::MutexLock m(mutex_);
    if (fail_) {
      return {-1, 1};
    }
    return Api::OsSysCallsImpl::socket(domain, type, protocol);
  }

private:
  absl::Mutex mutex_;
  bool fail_ = false;
};

TEST_P(ProtocolIntegrationTest, HandleUpstreamSocketCreationFail) {
  AllowForceFail fail_socket_n_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&fail_socket_n_};

  initialize();

#ifdef ENVOY_ENABLE_QUIC
  // It turns out on some builds, the ENVOY_BUG expected below slows the server
  // long enough to trigger QUIC blackhole detection, which results in the
  // client killing the connection before the 503 is delivered. Turn off QUIC
  // blackhole detection to avoid flaky tsan builds.
  if (downstreamProtocol() == Http::CodecType::HTTP3) {
    quic::QuicTagVector connection_options{quic::kNBHD};
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetConnectionOptionsToSend(connection_options);
  }
#endif

  codec_client_ = makeHttpConnection(lookupPort("http"));
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterGe("listener.127.0.0.1_0.downstream_cx_total", 1);
  } else {
    test_server_->waitForCounterGe("listener.[__1]_0.downstream_cx_total", 1);
  }

  EXPECT_ENVOY_BUG(
      {
        fail_socket_n_.startFailing();
        auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
        ASSERT_TRUE(response->waitForEndStream());
        EXPECT_EQ("503", response->headers().getStatusValue());
      },
      "");

  test_server_.reset();
  cleanupUpstreamAndDownstream();
  fake_upstreams_.clear();
}
#endif

TEST_P(ProtocolIntegrationTest, NoLocalInterfaceNameForUpstreamConnection) {
  config_helper_.prependFilter(R"EOF(
  name: stream-info-to-headers-filter
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make sure that the body was injected to the request.
  EXPECT_TRUE(upstream_request_->complete());

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the local interface name was not populated. This is the runtime default.
  EXPECT_TRUE(response->headers().get(Http::LowerCaseString("local_interface_name")).empty());
}

// WIN32 fails configuration and terminates the server.
#ifndef WIN32
TEST_P(ProtocolIntegrationTest, LocalInterfaceNameForUpstreamConnection) {
  config_helper_.prependFilter(R"EOF(
  name: stream-info-to-headers-filter
  )EOF");

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_clusters(0)
        ->mutable_upstream_connection_options()
        ->set_set_local_interface_name_on_upstream_connections(true);
  });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a headers only request.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Make sure that the body was injected to the request.
  EXPECT_TRUE(upstream_request_->complete());

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Make sure that the local interface name was populated due to runtime override.
  // TODO: h3 upstreams don't have local interface name
  if (GetParam().upstream_protocol == Http::CodecType::HTTP3) {
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("local_interface_name")).empty());
  } else {
    EXPECT_FALSE(response->headers().get(Http::LowerCaseString("local_interface_name")).empty());
  }
}
#endif

TEST_P(DownstreamProtocolIntegrationTest, InvalidRequestHeaderName) {
  // TODO(yanavlasov): remove runtime override after making disable_client_header_validation_ work
  // for non UHV builds
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  disable_client_header_validation_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // } is invalid character in header name
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {":scheme", "http"},
                                                                 {"foo}name", "foo_value"}});
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
    test_server_->waitForCounterGe("http.config_test.downstream_rq_4xx", 1);
  } else {
    // H/2 codec does not send 400 on protocol errors
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, InvalidRequestHeaderNameStreamError) {
  // TODO(yanavlasov): remove runtime override after making disable_client_header_validation_ work
  // for non UHV builds
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  disable_client_header_validation_ = true;
  // For H/1 this test is equivalent to InvalidRequestHeaderName
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  setDownstreamOverrideStreamErrorOnInvalidHttpMessage();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // } is invalid character in header name
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {":scheme", "http"},
                                                                 {"foo}name", "foo_value"}});
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(response->waitForReset());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
    test_server_->waitForCounterGe("http.config_test.downstream_rq_4xx", 1);
  } else {
    // H/2 codec does not send 400 on protocol errors
    EXPECT_EQ((downstream_protocol_ == Http::CodecType::HTTP3
                   ? Http::StreamResetReason::ProtocolError
                   : Http::StreamResetReason::RemoteReset),
              response->resetReason());
  }
}

TEST_P(ProtocolIntegrationTest, InvalidResponseHeaderName) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");

  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  // } is invalid character in header name
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"foo}name", "foo_value"}}, false);
  upstream_request_->encodeData(1, true);

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("502", response->headers().getStatusValue());
  test_server_->waitForCounterGe("http.config_test.downstream_rq_5xx", 1);
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    EXPECT_EQ(waitForAccessLog(access_log_name_),
              "upstream_reset_before_response_started{protocol_"
              "error|QUIC_HTTP_FRAME_ERROR|FROM_SELF|Invalid_headers}");
  } else {
    EXPECT_EQ(waitForAccessLog(access_log_name_),
              "upstream_reset_before_response_started{protocol_error}");
  }
}

TEST_P(ProtocolIntegrationTest, InvalidResponseHeaderNameStreamError) {
  // For H/1 this test is equivalent to InvalidResponseHeaderName
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  setUpstreamOverrideStreamErrorOnInvalidHttpMessage();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  // } is invalid character in header name
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"foo}name", "foo_value"}}, false);

  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("502", response->headers().getStatusValue());
  test_server_->waitForCounterGe("http.config_test.downstream_rq_5xx", 1);

  std::string error_message = upstreamProtocol() == Http::CodecType::HTTP3
                                  ? "upstream_reset_before_response_started{protocol_error|QUIC_"
                                    "BAD_APPLICATION_PAYLOAD|FROM_SELF}"
                                  : "upstream_reset_before_response_started{protocol_error}";

  EXPECT_EQ(waitForAccessLog(access_log_name_), error_message);
  // Upstream connection should stay up
  ASSERT_TRUE(fake_upstream_connection_->connected());
}

// Validate that when allow_multiplexed_upstream_half_close is enabled a request with H/2 or H/3
// upstream is not reset when upstream half closes before downstream and allows downstream to send
// data even if upstream is half closed.
// H/1 upstream always causes the stream to be closed if it responds before downstream.
// This test also causes downstream connection to run out of stream window (in H/2 and H/3 case)
// when processing END_STREAM from the server to make sure the data is not lost in the case.
TEST_P(ProtocolIntegrationTest, ServerHalfCloseBeforeClientWithBufferedResponseData) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.quic_fix_defer_logging_miss_for_half_closed_stream", "true");

  useAccessLog("%DURATION% %ROUNDTRIP_DURATION% %REQUEST_DURATION% %REQUEST_TX_DURATION% "
               "%RESPONSE_DURATION% %RESPONSE_TX_DURATION%");
  constexpr uint32_t kStreamWindowSize = 64 * 1024;
  // Set buffer limit large enough to accommodate H/2 stream window, so we can cause downstream
  // codec to buffer data without pushing back on upstream.
  config_helper_.setBufferLimits(kStreamWindowSize * 2, kStreamWindowSize * 2);

  initialize();
  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())
          .value();
  http2_options.mutable_initial_stream_window_size()->set_value(kStreamWindowSize);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":authority", "foo.lyft.com"}, {":path", "/"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Stop downstream client from updating the window (or reading for H/1)
  request_encoder_->getStream().readDisable(true);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  // Make downstream stream to run out of window
  upstream_request_->encodeData(kStreamWindowSize, false);
  // And cause the rest of data to the downstream to be buffered
  upstream_request_->encodeData(kStreamWindowSize / 4, true);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    // H/1 upstream always causes the stream to be closed if it responds before downstream.
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    if (downstreamProtocol() == Http::CodecType::HTTP1) {
      request_encoder_->getStream().readDisable(false);
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    } else if (downstreamProtocol() == Http::CodecType::HTTP2) {
      ASSERT_TRUE(response->waitForReset());
    } else if (downstreamProtocol() == Http::CodecType::HTTP3) {
      // Unlike H/2 codec H/3 codec attempts to send pending data before the reset
      // So it needs window to push the data to the client and then the reset
      // which just gets discarded since end_stream has been received just before
      // reset.
      request_encoder_->getStream().readDisable(false);
      ASSERT_TRUE(response->waitForEndStream());
      codec_client_->close();
    }
  } else if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP2 ||
             fake_upstreams_[0]->httpType() == Http::CodecType::HTTP3) {
    // H/2 or H/3 upstreams should allow downstream to send data even after upstream has half
    // closed.
    request_encoder_->getStream().readDisable(false);
    if (downstreamProtocol() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(response->waitForEndStream());
      ASSERT_TRUE(response->complete());
      ASSERT_EQ(80 * 1024, response->body().length());
      // Codec client does not allow us to gracefully finish the request, since
      // as soon it observes completion on H/1 response it closes the entire stream.
      // The H2UpstreamHalfCloseBeforeH1Downstream test that uses TCP client to fully
      // test this case.
      codec_client_->close();
      ASSERT_TRUE(upstream_request_->waitForReset());
    } else if (downstreamProtocol() == Http::CodecType::HTTP2 ||
               downstreamProtocol() == Http::CodecType::HTTP3) {
      ASSERT_TRUE(response->waitForEndStream());
      std::string data(128, 'a');
      ASSERT_TRUE(response->complete());
      ASSERT_EQ(80 * 1024, response->body().length());
      Buffer::OwnedImpl buffer(data);
      request_encoder_->encodeData(buffer, true);
      ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 128));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    }
  }

  std::string log = waitForAccessLog(access_log_name_);
  std::vector<std::string> timings = absl::StrSplit(log, ' ');
  ASSERT_EQ(timings.size(), 6);
  if (fake_upstreams_[0]->httpType() != Http::CodecType::HTTP1 &&
      downstreamProtocol() != Http::CodecType::HTTP1) {
    // All duration values except for ROUNDTRIP_DURATION should be present (no '-' in the access
    // log) when neither upstream nor downstream is H/1
    EXPECT_GE(/* DURATION */ std::stoi(timings.at(0)), 0);
    if (downstreamProtocol() == Http::CodecType::HTTP3) {
      // Only H/3 populate this metric.
      EXPECT_GT(/* ROUNDTRIP_DURATION */ std::stoi(timings.at(1)), 0);
    }
    EXPECT_GE(/* REQUEST_DURATION */ std::stoi(timings.at(2)), 0);
    EXPECT_GE(/* REQUEST_TX_DURATION */ std::stoi(timings.at(3)), 0);
    EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(timings.at(4)), 0);
    EXPECT_GE(/* RESPONSE_TX_DURATION */ std::stoi(timings.at(5)), 0);
  } else {
    // When one the peers is H/1 the stream is reset and request duration values will be unset
    EXPECT_GE(/* DURATION */ std::stoi(timings.at(0)), 0);
    EXPECT_EQ(/* ROUNDTRIP_DURATION */ timings.at(1), "-");
    EXPECT_EQ(/* REQUEST_DURATION */ timings.at(2), "-");
    EXPECT_EQ(/* REQUEST_TX_DURATION */ timings.at(3), "-");
    EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(timings.at(4)), 0);
  }
}

// Verify that even with upstream half close enabled the error response from
// upstream causes the request to be reset.
TEST_P(ProtocolIntegrationTest, ServerHalfCloseWithErrorBeforeClient) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");

  initialize();
  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())
          .value();
  http2_options.mutable_initial_stream_window_size()->set_value(64 * 1024);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":authority", "foo.lyft.com"}, {":path", "/"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "400"}}, true);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else if (downstreamProtocol() == Http::CodecType::HTTP2) {
    ASSERT_TRUE(response->waitForReset());
  } else if (downstreamProtocol() == Http::CodecType::HTTP3) {
    ASSERT_TRUE(response->waitForEndStream());
  }
  ASSERT_TRUE(response->complete());
  ASSERT_EQ("400", response->headers().getStatusValue());
}

// Same as above but with data sent after the error response.
// Note the behavior is the same as when the allow_multiplexed_upstream_half_close is false.
TEST_P(ProtocolIntegrationTest, ServerHalfCloseBeforeClientWithErrorAndBufferedResponseData) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  constexpr uint32_t kStreamWindowSize = 64 * 1024;
  // Set buffer limit large enough to accommodate H/2 stream window
  config_helper_.setBufferLimits(kStreamWindowSize * 2, kStreamWindowSize * 2);

  initialize();
  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())
          .value();
  http2_options.mutable_initial_stream_window_size()->set_value(kStreamWindowSize);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":authority", "foo.lyft.com"}, {":path", "/"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Stop downstream client updating the window (or reading for H/1)
  request_encoder_->getStream().readDisable(true);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, false);
  // Make downstream stream to run out of window
  upstream_request_->encodeData(kStreamWindowSize, false);
  // And cause the rest of data to the downstream to be buffered
  upstream_request_->encodeData(kStreamWindowSize / 4, true);

  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    if (downstreamProtocol() == Http::CodecType::HTTP1) {
      request_encoder_->getStream().readDisable(false);
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    } else if (downstreamProtocol() == Http::CodecType::HTTP2) {
      ASSERT_TRUE(response->waitForReset());
    } else if (downstreamProtocol() == Http::CodecType::HTTP3) {
      // Unlike H/2, H/3 client codec only stops sending request upon STOP_SENDING frame but still
      // attempts to finish receiving response. So resume reading in order to fully close the
      // stream after receiving both STOP_SENDING and end stream.
      request_encoder_->getStream().readDisable(false);
      ASSERT_TRUE(response->waitForEndStream());
      // Following STOP_SENDING will be propagated via reset callback.
      ASSERT_TRUE(response->waitForReset());
    }
  } else if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP2 ||
             fake_upstreams_[0]->httpType() == Http::CodecType::HTTP3) {
    request_encoder_->getStream().readDisable(false);
    if (downstreamProtocol() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(response->waitForEndStream());
      ASSERT_TRUE(response->complete());
      ASSERT_EQ(80 * 1024, response->body().length());
      codec_client_->close();
      ASSERT_TRUE(upstream_request_->waitForReset());
    } else if (downstreamProtocol() == Http::CodecType::HTTP2 ||
               downstreamProtocol() == Http::CodecType::HTTP3) {
      ASSERT_TRUE(upstream_request_->waitForReset());
      ASSERT_TRUE(response->waitForAnyTermination());
    }
  }
  cleanupUpstreamAndDownstream();
}

TEST_P(ProtocolIntegrationTest, H2UpstreamHalfCloseBeforeH1Downstream) {
  // This test is only for H/1 downstream and H/2 or H/3 upstream
  // Other cases are covered by the ServerHalfCloseBeforeClientWithBufferedResponseData
  // It verifies that H/1 downstream request is not reset when H/2 upstream completes the stream
  // before the downstream and can still send data to the upstream.
  if (downstreamProtocol() != Http::CodecType::HTTP1 ||
      upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }

  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  useAccessLog("%DURATION% %REQUEST_DURATION% %REQUEST_TX_DURATION% %RESPONSE_DURATION% "
               "%RESPONSE_TX_DURATION%");
  constexpr uint32_t kStreamChunkSize = 64 * 1024;
  config_helper_.setBufferLimits(kStreamChunkSize * 2, kStreamChunkSize * 2);
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));

  ASSERT_TRUE(tcp_client->write(
      "POST / HTTP/1.1\r\nHost: foo.lyft.com\r\nTransfer-Encoding: chunked\r\n\r\n", false, false));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(kStreamChunkSize, false);
  upstream_request_->encodeData(kStreamChunkSize / 4, true);

  // Wait for the last chunk to arrive
  tcp_client->waitForData("\r\n0\r\n\r\n", false);
  ASSERT_TRUE(tcp_client->connected());

  // Now write data into downstream client after upstream has completed its response and verify that
  // upstream receives it.
  ASSERT_TRUE(tcp_client->write(absl::StrCat("80\r\n", std::string(0x80, 'A'), "\r\n0\r\n\r\n"),
                                false, false));

  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 0x80));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  tcp_client->close();
  std::string timing = waitForAccessLog(access_log_name_);
  // All duration values should be present (no '-' in the access log)
  ASSERT_FALSE(absl::StrContains(timing, '-'));
}

TEST_P(DownstreamProtocolIntegrationTest, DuplicatedSchemeHeaders) {
  disable_client_header_validation_ = true;
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    // send_fully_qualified_url_ is not enabled, so :scheme header isn't used.
    return;
  }
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {":scheme", "http"}});
  ASSERT_TRUE(response->waitForAnyTermination());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid"));
}

TEST_P(DownstreamProtocolIntegrationTest, DuplicatedMethodHeaders) {
  disable_client_header_validation_ = true;

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {":method", "POST"}});
  ASSERT_TRUE(response->waitForAnyTermination());
  EXPECT_THAT(
      waitForAccessLog(access_log_name_),
      HasSubstr(downstreamProtocol() == Http::CodecType::HTTP1 ? "codec_error" : "invalid"));
}

TEST_P(DownstreamProtocolIntegrationTest, MethodHeaderWithWhitespace) {
  disable_client_header_validation_ = true;
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET /admin"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});
  ASSERT_TRUE(response->waitForAnyTermination());
  EXPECT_THAT(
      waitForAccessLog(access_log_name_),
      HasSubstr(downstreamProtocol() == Http::CodecType::HTTP1 ? "codec_error" : "invalid"));
}

TEST_P(DownstreamProtocolIntegrationTest, EmptyMethodHeader) {
  disable_client_header_validation_ = true;
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", ""}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}});
  ASSERT_TRUE(response->waitForAnyTermination());
  EXPECT_THAT(
      waitForAccessLog(access_log_name_),
      HasSubstr(downstreamProtocol() == Http::CodecType::HTTP1 ? "codec_error" : "invalid"));
}

TEST_P(DownstreamProtocolIntegrationTest, InvalidSchemeHeaderWithWhitespace) {
  disable_client_header_validation_ = true;
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "/admin http"},
                                     {":authority", "sni.lyft.com"}});

  if (use_universal_header_validator_) {
    if (downstreamProtocol() != Http::CodecType::HTTP1) {
      ASSERT_TRUE(response->waitForReset());
      EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid"));
      return;
    }
  } else {
    if (downstreamProtocol() == Http::CodecType::HTTP2 &&
        GetParam().http2_implementation == Http2Impl::Nghttp2) {
      ASSERT_TRUE(response->waitForReset());
      EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid"));
      return;
    }
  }
  // Other HTTP codecs accept the bad scheme but the Envoy should replace it with a valid one.
  waitForNextUpstreamRequest();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // The scheme header is not conveyed in HTTP/1.
    EXPECT_EQ(nullptr, upstream_request_->headers().Scheme());
  } else {
    EXPECT_THAT(upstream_request_->headers(), ContainsHeader(Http::Headers::get().Scheme, "http"));
  }
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verify that request with invalid trailers is rejected.
TEST_P(DownstreamProtocolIntegrationTest, InvalidTrailer) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, false);
  Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}};
  // DEL (0x7F) is invalid header value
  Http::HeaderString invalid_value;
  invalid_value.setCopyUnvalidatedForTestOnly("abc\x7Fxyz");
  trailers.addViaMove(Http::HeaderString(absl::string_view("trailer2")), std::move(invalid_value));

  codec_client_->sendTrailers(*request_encoder_, trailers);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->waitForReset());
    codec_client_->close();
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }

  if (!use_universal_header_validator_) {
    // TODO(#24620) UHV does not include the DPE prefix in the downstream protocol error reasons
    if (downstreamProtocol() != Http::CodecType::HTTP3) {
      // TODO(#24630) QUIC also does not include the DPE prefix in the downstream protocol error
      // reasons
      EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
    }
  }
}

// Verify that stream is reset with invalid trailers, when configured.
TEST_P(DownstreamProtocolIntegrationTest, InvalidTrailerStreamError) {
  // H/1 requests are not affected by the override_stream_error_on_invalid_http_message
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    return;
  }
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
  config_helper_.addConfigModifier(setEnableUpstreamTrailersHttp1());
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        hcm.mutable_http3_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
        hcm.mutable_http2_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
        hcm.mutable_http_protocol_options()
            ->mutable_override_stream_error_on_invalid_http_message()
            ->set_value(true);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1, false);
  Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}};
  // DEL (0x7F) is invalid header value
  Http::HeaderString invalid_value;
  invalid_value.setCopyUnvalidatedForTestOnly("abc\x7Fxyz");
  trailers.addViaMove(Http::HeaderString(absl::string_view("trailer2")), std::move(invalid_value));

  codec_client_->sendTrailers(*request_encoder_, trailers);

  ASSERT_TRUE(response->waitForReset());
  codec_client_->close();
  ASSERT_TRUE(response->reset());
  EXPECT_EQ((downstreamProtocol() == Http::CodecType::HTTP3 ? Http::StreamResetReason::ProtocolError
                                                            : Http::StreamResetReason::RemoteReset),
            response->resetReason());
  if (!use_universal_header_validator_) {
    // TODO(#24620) UHV does not include the DPE prefix in the downstream protocol error reasons
    if (downstreamProtocol() != Http::CodecType::HTTP3) {
      // TODO(#24630) QUIC also does not include the DPE prefix in the downstream protocol error
      // reasons
      EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("DPE"));
    }
  }
}

TEST_P(DownstreamProtocolIntegrationTest, UnknownPseudoHeader) {
  disable_client_header_validation_ = true;
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":a", "evil.com"},
                                     {":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  ASSERT_TRUE(response->waitForReset());
  if (use_universal_header_validator_) {
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("invalid"));
  } else {
    EXPECT_THAT(waitForAccessLog(access_log_name_),
                HasSubstr((downstreamProtocol() == Http::CodecType::HTTP2 &&
                           GetParam().http2_implementation == Http2Impl::Oghttp2)
                              ? "violation"
                              : "invalid"));
  }
}

TEST_P(DownstreamProtocolIntegrationTest, ConfigureAsyncLbWhenUnsupported) {
  // Configure the async round robin load balancer but disable async host
  // selection. This should result in host selection failing.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.async_host_selection", "false");
  config_helper_.setAsyncLb();
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

TEST_P(DownstreamProtocolIntegrationTest, EmptyCookieHeader) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = default_request_headers_;
  request_headers.addCopy("cookie", "");
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(0);

  const bool has_empty_cookie =
      !upstream_request_->headers().get(Http::LowerCaseString("cookie")).empty();
  if (downstreamProtocol() == Http::CodecType::HTTP1 &&
      upstreamProtocol() == Http::CodecType::HTTP1) {
    EXPECT_TRUE(has_empty_cookie);
  } else {
    EXPECT_FALSE(has_empty_cookie);
  }
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
}

TEST_P(DownstreamProtocolIntegrationTest, DownstreamCxStats) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512U, response->body().size());

  test_server_->waitForCounterGe("http.config_test.downstream_cx_tx_bytes_total", 512);
}

// When upstream protocol is HTTP1, an OPTIONS request with no body will not
// append transfer-encoding chunked.
TEST_P(DownstreamProtocolIntegrationTest, OptionsWithNoBodyNotChunked) {
  // This test is only relevant to H1 upstream.
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "OPTIONS"},
      {":path", "/foo"},
      {":scheme", "http"},
      {":authority", "host"},
      {"access-control-request-method", "GET"},
      {"origin", "test-origin"},
  };
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().TransferEncoding(), nullptr);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  EXPECT_EQ(response->headers().TransferEncoding(), nullptr);
}

} // namespace Envoy
