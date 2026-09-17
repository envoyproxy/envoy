#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using BodySizeLimitIntegrationTest = UpstreamDownstreamIntegrationTest;

INSTANTIATE_TEST_SUITE_P(
    Protocols, BodySizeLimitIntegrationTest,
    testing::ValuesIn(UpstreamDownstreamIntegrationTest::getDefaultTestParams()),
    UpstreamDownstreamIntegrationTest::testParamsToString);

TEST_P(BodySizeLimitIntegrationTest, RouterNotFoundBodyBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBodySizeLimitFilter(),
                               testing_downstream_filter_);
  testRouterNotFoundWithBody();
}

TEST_P(BodySizeLimitIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  if (upstreamProtocol() == Http::CodecType::HTTP3 ||
      downstreamProtocol() == Http::CodecType::HTTP3) {
    // TODO(#26236) - Fix test flakiness over HTTP/3.
    return;
  }
  config_helper_.prependFilter(ConfigHelper::defaultBodySizeLimitFilter(),
                               testing_downstream_filter_);
  testRouterRequestAndResponseWithBody(4 * 1024 * 1024, 4 * 1024 * 1024, false);
}

TEST_P(BodySizeLimitIntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBodySizeLimitFilter(),
                               testing_downstream_filter_);
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(BodySizeLimitIntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBodySizeLimitFilter(),
                               testing_downstream_filter_);
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(BodySizeLimitIntegrationTest, RouterRequestAndResponseWithZeroByteBodyBuffer) {
  config_helper_.prependFilter(ConfigHelper::defaultBodySizeLimitFilter(),
                               testing_downstream_filter_);
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(BodySizeLimitIntegrationTest, RouterRequestSizeLimitExceeded) {
  // Make sure the connection isn't closed during request upload.
  // Without a large drain-close it's possible that the local reply will be sent
  // during request upload, and continued upload will result in TCP reset before
  // the response is read.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(2000 * 1000); });
  config_helper_.prependFilter(ConfigHelper::smallBodySizeLimitFilter(),
                               testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

TEST_P(BodySizeLimitIntegrationTest, RouterHeaderRequestSizeLimitExceeded) {
  // Make sure the connection isn't closed during request upload.
  // Without a large drain-close it's possible that the local reply will be sent
  // during request upload, and continued upload will result in TCP reset before
  // the response is read.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(2000 * 1000); });
  config_helper_.prependFilter(ConfigHelper::smallBodySizeLimitFilter(),
                               testing_downstream_filter_);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"},
                                     {"content-length", "66560"}},
      1024 * 60, false);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

// Test that the filter can be disabled on a specific route using the generic
// FilterConfig{disabled: true} mechanism in typed_per_filter_config.
TEST_P(BodySizeLimitIntegrationTest, RouteDisabled) {
  if (!testing_downstream_filter_) {
    return;
  }
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) {
        envoy::config::route::v3::FilterConfig per_route_config;
        per_route_config.set_disabled(true);

        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_typed_per_filter_config();

        std::ignore = (*config)["body_size_limit"].PackFrom(per_route_config);
      });
  config_helper_.prependFilter(ConfigHelper::smallBodySizeLimitFilter(),
                               testing_downstream_filter_);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"content-length", "66560"}},
      1024 * 65);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that the body size limit can be overridden per-route using the filter_chain filter.
// The main filter chain has a small limit (1024 bytes), but the per-route filter_chain
// configuration applies a larger limit (5MB), allowing the request to pass through.
TEST_P(BodySizeLimitIntegrationTest, RouteOverrideViaFilterChain) {
  if (!testing_downstream_filter_) {
    return;
  }

  // Add the filter_chain filter with the body_size_limit as the default chain filter.
  const std::string filter_chain_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  default_filter_chain:
    filters:
    - name: body_size_limit
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.body_size_limit.v3.BodySizeLimit
        max_request_bytes: 1024
)EOF";

  config_helper_.prependFilter(filter_chain_config, testing_downstream_filter_);

  // Add a per-route filter_chain config that overrides the body_size_limit with a larger limit.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cfg) {
        envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute per_route;
        auto* filter = per_route.mutable_filter_chain()->add_filters();
        filter->set_name("body_size_limit");
        envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit override_config;
        override_config.mutable_max_request_bytes()->set_value(5242880);
        std::ignore = filter->mutable_typed_config()->PackFrom(override_config);

        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_typed_per_filter_config();

        std::ignore = (*config)["envoy.filters.http.filter_chain"].PackFrom(per_route);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"}},
      1024 * 65);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test that without the per-route override, the default filter_chain limit rejects the request.
// This is the complement of RouteOverrideViaFilterChain — same setup without the per-route config.
TEST_P(BodySizeLimitIntegrationTest, FilterChainDefaultLimitRejects) {
  if (!testing_downstream_filter_) {
    return;
  }

  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(2000 * 1000); });

  const std::string filter_chain_config = R"EOF(
name: envoy.filters.http.filter_chain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  default_filter_chain:
    filters:
    - name: body_size_limit
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.body_size_limit.v3.BodySizeLimit
        max_request_bytes: 1024
)EOF";

  config_helper_.prependFilter(filter_chain_config, testing_downstream_filter_);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "sni.lyft.com"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65, false);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
