#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

const std::string RBAC_CONFIG = R"EOF(
name: envoy.filters.http.rbac
config:
  rules:
    policies:
      foo:
        permissions:
          - header: { name: ":method", exact_match: "GET" }
        principals:
          - any: true
)EOF";

typedef HttpProtocolIntegrationTest RBACIntegrationTest;

INSTANTIATE_TEST_CASE_P(Protocols, RBACIntegrationTest,
                        testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                        HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(RBACIntegrationTest, Allowed) {
  config_helper_.addFilter(RBAC_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "host"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(RBACIntegrationTest, Denied) {
  config_helper_.addFilter(RBAC_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "host"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("403", response->headers().Status()->value().c_str());
}

TEST_P(RBACIntegrationTest, DeniedHeadReply) {
  config_helper_.addFilter(RBAC_CONFIG);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestHeaderMapImpl{
          {":method", "HEAD"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "host"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("403", response->headers().Status()->value().c_str());
  ASSERT_TRUE(response->headers().ContentLength());
  EXPECT_STRNE("0", response->headers().ContentLength()->value().c_str());
  EXPECT_STREQ("", response->body().c_str());
}

TEST_P(RBACIntegrationTest, RouteOverride) {
  config_helper_.addConfigModifier(
      [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& cfg) {
        ProtobufWkt::Struct pfc;
        ASSERT_TRUE(Protobuf::util::JsonStringToMessage("{}", &pfc).ok());

        auto* config = cfg.mutable_route_config()
                           ->mutable_virtual_hosts()
                           ->Mutable(0)
                           ->mutable_per_filter_config();

        (*config)[Extensions::HttpFilters::HttpFilterNames::get().Rbac] = pfc;
      });
  config_helper_.addFilter(RBAC_CONFIG);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(
      Http::TestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "host"},
          {"x-forwarded-for", "10.0.0.1"},
      },
      1024);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

} // namespace
} // namespace Envoy
