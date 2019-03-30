#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/integration/http_protocol_integration.h"

#include "fmt/printf.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::envoy::config::filter::network::http_connection_manager::v2::HttpFilter;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

std::string getAuthFilterConfig(const std::string& config_str, bool use_local_jwks) {
  JwtAuthentication proto_config;
  MessageUtil::loadFromYaml(config_str, proto_config);

  if (use_local_jwks) {
    auto& provider0 = (*proto_config.mutable_providers())[std::string(ProviderName)];
    provider0.clear_remote_jwks();
    auto local_jwks = provider0.mutable_local_jwks();
    local_jwks->set_inline_string(PublicKey);
  }

  HttpFilter filter;
  filter.set_name(HttpFilterNames::get().JwtAuthn);
  MessageUtil::jsonConvert(proto_config, *filter.mutable_config());
  return MessageUtil::getJsonStringFromMessage(filter);
}

std::string getFilterConfig(bool use_local_jwks) {
  return getAuthFilterConfig(ExampleConfig, use_local_jwks);
}

typedef HttpProtocolIntegrationTest LocalJwksIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, LocalJwksIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// With local Jwks, this test verifies a request is passed with a good Jwt token.
TEST_P(LocalJwksIntegrationTest, WithGoodToken) {
  config_helper_.addFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForNextUpstreamRequest();
  const auto* payload_entry =
      upstream_request_->headers().get(Http::LowerCaseString("sec-istio-auth-userinfo"));
  EXPECT_TRUE(payload_entry != nullptr);
  EXPECT_EQ(payload_entry->value().getStringView(), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_FALSE(upstream_request_->headers().Authorization());
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

// With local Jwks, this test verifies a request is rejected with an expired Jwt token.
TEST_P(LocalJwksIntegrationTest, ExpiredToken) {
  config_helper_.addFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(ExpiredToken)},
  });

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("401", response->headers().Status()->value().c_str());
}

TEST_P(LocalJwksIntegrationTest, MissingToken) {
  config_helper_.addFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("401", response->headers().Status()->value().c_str());
}

TEST_P(LocalJwksIntegrationTest, ExpiredTokenHeadReply) {
  config_helper_.addFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "HEAD"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(ExpiredToken)},
  });

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("401", response->headers().Status()->value().c_str());
  EXPECT_STRNE("0", response->headers().ContentLength()->value().c_str());
  EXPECT_STREQ("", response->body().c_str());
}

// This test verifies a request is passed with a path that don't match any requirements.
TEST_P(LocalJwksIntegrationTest, NoRequiresPath) {
  config_helper_.addFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/foo"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

// This test verifies JwtRequirement specified from metadata
TEST_P(LocalJwksIntegrationTest, MetadataRequirement) {
  const std::string meta_filter_conf = R"(
name: %s
config:
  request_rules:
    - header: selector
      on_header_present:
        metadata_namespace: selector_filter
        key: selector
)";

  // A config with metadata rules.
  const std::string auth_filter_conf = R"(
  providers:
    example_provider:
      issuer: https://example.com
      audiences:
      - example_service
  metadata_rules:
    filter: selector_filter
    path:
    - selector
    requires:
      example_provider:
        provider_name: example_provider
)";

  config_helper_.addFilter(getAuthFilterConfig(auth_filter_conf, true));
  config_helper_.addFilter(fmt::sprintf(meta_filter_conf, HttpFilterNames::get().HeaderToMetadata));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  struct TestCase {
    std::vector<std::pair<std::string, std::string>> extra_headers;
    std::string expected_status;
  };

  const TestCase test_cases[] = {
      // Case1: not set metadata, so Jwt is not required, expect 200
      {
          // Empty extra headers
          {},
          "200",
      },

      // Case2: requirement is set in the metadata, but missing token, expect 401
      {
          // selector header, but not token header
          {
              {"selector", "example_provider"},
          },
          "401",
      },

      // Case 3: requirement is set in the metadata, token is good, expect 200
      {
          // selector header, and token header
          {
              {"selector", "example_provider"},
              {"Authorization", "Bearer " + std::string(GoodToken)},
          },
          "200",
      },
  };

  for (const auto& test : test_cases) {
    Http::TestHeaderMapImpl headers{
        {":method", "GET"},
        {":path", "/foo"},
        {":scheme", "http"},
        {":authority", "host"},
    };
    for (const auto& h : test.extra_headers) {
      headers.addCopy(h.first, h.second);
    }
    auto response = codec_client_->makeHeaderOnlyRequest(headers);

    if (test.expected_status == "200") {
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
    }

    response->waitForEndStream();
    ASSERT_TRUE(response->complete());
    EXPECT_EQ(test.expected_status, response->headers().Status()->value().c_str());
  }
}

// The test case with a fake upstream for remote Jwks server.
class RemoteJwksIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void createUpstreams() override {
    HttpProtocolIntegrationTest::createUpstreams();
    // for Jwks upstream.
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, GetParam().upstream_protocol, version_, timeSystem()));
  }

  void initializeFilter() {
    config_helper_.addFilter(getFilterConfig(false));

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* jwks_cluster = bootstrap.mutable_static_resources()->add_clusters();
      jwks_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      jwks_cluster->set_name("pubkey_cluster");
    });

    initialize();
  }

  void waitForJwksResponse(const std::string& status, const std::string& jwks_body) {
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_jwks_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_jwks_connection_->waitForNewStream(*dispatcher_, jwks_request_);
    RELEASE_ASSERT(result, result.message());
    result = jwks_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    Http::TestHeaderMapImpl response_headers{{":status", status}};
    jwks_request_->encodeHeaders(response_headers, false);
    Buffer::OwnedImpl response_data1(jwks_body);
    jwks_request_->encodeData(response_data1, true);
  }

  void cleanup() {
    codec_client_->close();
    if (fake_jwks_connection_ != nullptr) {
      AssertionResult result = fake_jwks_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_jwks_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    if (fake_upstream_connection_ != nullptr) {
      AssertionResult result = fake_upstream_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_upstream_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_jwks_connection_{};
  FakeStreamPtr jwks_request_{};
};

INSTANTIATE_TEST_SUITE_P(Protocols, RemoteJwksIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// With remote Jwks, this test verifies a request is passed with a good Jwt token
// and a good public key fetched from a remote server.
TEST_P(RemoteJwksIntegrationTest, WithGoodToken) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForJwksResponse("200", PublicKey);

  waitForNextUpstreamRequest();

  const auto* payload_entry =
      upstream_request_->headers().get(Http::LowerCaseString("sec-istio-auth-userinfo"));
  EXPECT_TRUE(payload_entry != nullptr);
  EXPECT_EQ(payload_entry->value().getStringView(), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_FALSE(upstream_request_->headers().Authorization());

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  cleanup();
}

// With remote Jwks, this test verifies a request is rejected even with a good Jwt token
// when the remote jwks server replied with 500.
TEST_P(RemoteJwksIntegrationTest, FetchFailedJwks) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  // Fails the jwks fetching.
  waitForJwksResponse("500", "");

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("401", response->headers().Status()->value().c_str());

  cleanup();
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
