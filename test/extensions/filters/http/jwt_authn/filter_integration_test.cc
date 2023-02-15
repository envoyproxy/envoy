#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/router/string_accessor_impl.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/integration/filters/header_to_filter_state.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

std::string getAuthFilterConfig(const std::string& config_str, bool use_local_jwks) {
  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config_str, proto_config);

  if (use_local_jwks) {
    auto& provider0 = (*proto_config.mutable_providers())[std::string(ProviderName)];
    provider0.clear_remote_jwks();
    auto local_jwks = provider0.mutable_local_jwks();
    local_jwks->set_inline_string(PublicKey);
  }

  HttpFilter filter;
  filter.set_name("envoy.filters.http.jwt_authn");
  filter.mutable_typed_config()->PackFrom(proto_config);
  return MessageUtil::getJsonStringFromMessageOrDie(filter);
}

std::string getAsyncFetchFilterConfig(const std::string& config_str, bool fast_listener) {
  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config_str, proto_config);

  auto& provider0 = (*proto_config.mutable_providers())[std::string(ProviderName)];
  auto* async_fetch = provider0.mutable_remote_jwks()->mutable_async_fetch();
  async_fetch->set_fast_listener(fast_listener);
  // Set failed_refetch_duration to a big value to disable failed refetch.
  // as it interferes the two FailedAsyncFetch integration tests.
  async_fetch->mutable_failed_refetch_duration()->set_seconds(600);

  HttpFilter filter;
  filter.set_name("envoy.filters.http.jwt_authn");
  filter.mutable_typed_config()->PackFrom(proto_config);
  return MessageUtil::getJsonStringFromMessageOrDie(filter);
}

std::string getFilterConfig(bool use_local_jwks) {
  return getAuthFilterConfig(ExampleConfig, use_local_jwks);
}

class LocalJwksIntegrationTest : public HttpProtocolIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(Protocols, LocalJwksIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// With local Jwks, this test verifies a request is passed with a good Jwt token.
TEST_P(LocalJwksIntegrationTest, WithGoodToken) {
  config_helper_.prependFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForNextUpstreamRequest();
  const auto payload_entry =
      upstream_request_->headers().get(Http::LowerCaseString("sec-istio-auth-userinfo"));
  EXPECT_FALSE(payload_entry.empty());
  EXPECT_EQ(payload_entry[0]->value().getStringView(), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_TRUE(upstream_request_->headers().get(Http::CustomHeaders::get().Authorization).empty());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// With local Jwks, this test verifies a request is rejected with an expired Jwt token.
TEST_P(LocalJwksIntegrationTest, ExpiredToken) {
  config_helper_.prependFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(ExpiredToken)},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(1, response->headers().get(Http::Headers::get().WWWAuthenticate).size());
  EXPECT_EQ(
      "Bearer realm=\"http://host/\", error=\"invalid_token\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
}

TEST_P(LocalJwksIntegrationTest, MissingToken) {
  config_helper_.prependFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(
      "Bearer realm=\"http://host/\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
}

TEST_P(LocalJwksIntegrationTest, ExpiredTokenHeadReply) {
  config_helper_.prependFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "HEAD"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(ExpiredToken)},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(
      "Bearer realm=\"http://host/\", error=\"invalid_token\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

  EXPECT_NE("0", response->headers().getContentLengthValue());
  EXPECT_THAT(response->body(), ::testing::IsEmpty());
}

// This test verifies a request is passed with a path that don't match any requirements.
TEST_P(LocalJwksIntegrationTest, NoRequiresPath) {
  config_helper_.prependFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/foo"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// This test verifies a CORS preflight request without JWT token is allowed.
TEST_P(LocalJwksIntegrationTest, CorsPreflight) {
  config_helper_.prependFilter(getFilterConfig(true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "OPTIONS"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"access-control-request-method", "GET"},
      {"origin", "test-origin"},
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// This test verifies JwtRequirement specified from filter state rules
TEST_P(LocalJwksIntegrationTest, FilterStateRequirement) {
  // A config with metadata rules.
  const std::string auth_filter_conf = R"(
  providers:
    example_provider:
      issuer: https://example.com
      audiences:
      - example_service
  filter_state_rules:
    name: jwt_selector
    requires:
      example_provider:
        provider_name: example_provider
)";

  config_helper_.prependFilter(getAuthFilterConfig(auth_filter_conf, true));
  config_helper_.prependFilter(R"(
  name: header-to-filter-state
  typed_config:
    "@type": type.googleapis.com/test.integration.filters.HeaderToFilterStateFilterConfig
    header_name: jwt_selector
    state_name: jwt_selector
    read_only: true
)");
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
              {"jwt_selector", "example_provider"},
          },
          "401",
      },

      // Case 3: requirement is set in the metadata, token is good, expect 200
      {
          // selector header, and token header
          {
              {"jwt_selector", "example_provider"},
              {"Authorization", "Bearer " + std::string(GoodToken)},
          },
          "200",
      },
  };

  for (const auto& test : test_cases) {
    Http::TestRequestHeaderMapImpl headers{
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
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    }

    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ(test.expected_status, response->headers().getStatusValue());
  }
}

// Verify that JWT config with RegEx matcher can handle CONNECT requests.
TEST_P(LocalJwksIntegrationTest, ConnectRequestWithRegExMatch) {
  config_helper_.prependFilter(getAuthFilterConfig(ExampleConfigWithRegEx, true));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "CONNECT"},
      {":authority", "host.com:80"},
      {"authorization", "Bearer " + std::string(GoodToken)},
  });
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Because CONNECT requests do not include a path, they will fail
  // to find a route match and return a 404.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// The test case with a fake upstream for remote Jwks server.
class RemoteJwksIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void createUpstreams() override {
    HttpProtocolIntegrationTest::createUpstreams();
    // for Jwks upstream.
    addFakeUpstream(GetParam().upstream_protocol);
  }

  void initializeFilter(bool add_cluster) {
    config_helper_.prependFilter(getFilterConfig(false));

    if (add_cluster) {
      config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* jwks_cluster = bootstrap.mutable_static_resources()->add_clusters();
        jwks_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        jwks_cluster->set_name("pubkey_cluster");
      });
    } else {
      config_helper_.skipPortUsageValidation();
    }

    initialize();
  }

  void initializeAsyncFetchFilter(bool fast_listener) {
    config_helper_.prependFilter(getAsyncFetchFilterConfig(ExampleConfig, fast_listener));

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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

    Http::TestResponseHeaderMapImpl response_headers{{":status", status}};
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
  initializeFilter(/*add_cluster=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForJwksResponse("200", PublicKey);

  waitForNextUpstreamRequest();

  const auto payload_entry =
      upstream_request_->headers().get(Http::LowerCaseString("sec-istio-auth-userinfo"));
  EXPECT_FALSE(payload_entry.empty());
  EXPECT_EQ(payload_entry[0]->value().getStringView(), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_TRUE(upstream_request_->headers().get(Http::CustomHeaders::get().Authorization).empty());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

// With remote Jwks, this test verifies a request is rejected even with a good Jwt token
// when the remote jwks server replied with 500.
TEST_P(RemoteJwksIntegrationTest, FetchFailedJwks) {
  initializeFilter(/*add_cluster=*/true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  // Fails the jwks fetching.
  waitForJwksResponse("500", "");

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(
      "Bearer realm=\"http://host/\", error=\"invalid_token\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());

  cleanup();
}

TEST_P(RemoteJwksIntegrationTest, FetchFailedMissingCluster) {
  initializeFilter(/*add_cluster=*/false);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(
      "Bearer realm=\"http://host/\", error=\"invalid_token\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
  cleanup();
}

TEST_P(RemoteJwksIntegrationTest, WithGoodTokenAsyncFetch) {
  on_server_init_function_ = [this]() { waitForJwksResponse("200", PublicKey); };
  initializeAsyncFetchFilter(false);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForNextUpstreamRequest();

  const auto payload_entry =
      upstream_request_->headers().get(Http::LowerCaseString("sec-istio-auth-userinfo"));
  EXPECT_FALSE(payload_entry.empty());
  EXPECT_EQ(payload_entry[0]->value().getStringView(), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_TRUE(upstream_request_->headers().get(Http::CustomHeaders::get().Authorization).empty());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

TEST_P(RemoteJwksIntegrationTest, WithGoodTokenAsyncFetchFast) {
  on_server_init_function_ = [this]() { waitForJwksResponse("200", PublicKey); };
  initializeAsyncFetchFilter(true);

  // This test is only expecting one jwks fetch, but there is a race condition in the test:
  // In fast fetch mode, the listener is activated without waiting for jwks fetch to be
  // completed. When the first request comes at the worker thread, jwks fetch could be at
  // any state at the main thread. If its result is not saved into jwks thread local slot,
  // the first request will trigger a second jwks fetch, this is not expected, test will fail.
  // To avoid such race condition, before making the first request, wait for the first
  // fetch stats to be updated.
  test_server_->waitForCounterGe("http.config_test.jwt_authn.jwks_fetch_success", 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForNextUpstreamRequest();

  const auto payload_entry =
      upstream_request_->headers().get(Http::LowerCaseString("sec-istio-auth-userinfo"));
  EXPECT_FALSE(payload_entry.empty());
  EXPECT_EQ(payload_entry[0]->value().getStringView(), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_TRUE(upstream_request_->headers().get(Http::CustomHeaders::get().Authorization).empty());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanup();
}

TEST_P(RemoteJwksIntegrationTest, WithFailedJwksAsyncFetch) {
  on_server_init_function_ = [this]() { waitForJwksResponse("500", ""); };
  initializeAsyncFetchFilter(false);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());

  cleanup();
}

TEST_P(RemoteJwksIntegrationTest, WithFailedJwksAsyncFetchFast) {
  on_server_init_function_ = [this]() { waitForJwksResponse("500", ""); };
  initializeAsyncFetchFilter(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());

  cleanup();
}

class PerRouteIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void setup(const std::string& filter_config, const PerRouteConfig& per_route) {
    config_helper_.prependFilter(getAuthFilterConfig(filter_config, true));

    config_helper_.addConfigModifier(
        [per_route](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          auto& per_route_any =
              (*virtual_host->mutable_routes(0)
                    ->mutable_typed_per_filter_config())["envoy.filters.http.jwt_authn"];
          per_route_any.PackFrom(per_route);
        });

    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, PerRouteIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// This test verifies per-route config disabled.
TEST_P(PerRouteIntegrationTest, PerRouteConfigDisabled) {
  // per-route config has disabled flag.
  PerRouteConfig per_route;
  per_route.set_disabled(true);
  // Use a normal filter config that requires jwt_auth.
  setup(ExampleConfig, per_route);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // So the request without a JWT token is OK.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// This test verifies per-route config with wrong requirement_name
TEST_P(PerRouteIntegrationTest, PerRouteConfigWrongRequireName) {
  // A config with a requirement_map
  const std::string filter_conf = R"(
  providers:
    example_provider:
      issuer: https://example.com
      audiences:
      - example_service
  requirement_map:
    abc:
      provider_name: "example_provider"
)";

  // Per-route config has a wrong requirement_name.
  PerRouteConfig per_route;
  per_route.set_requirement_name("wrong-requirement-name");
  setup(filter_conf, per_route);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // So the request with a good Jwt token is rejected.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// This test verifies per-route config with correct requirement_name
TEST_P(PerRouteIntegrationTest, PerRouteConfigOK) {
  // A config with a requirement_map
  const std::string filter_conf = R"(
  providers:
    example_provider:
      issuer: https://example.com
      audiences:
      - example_service
  requirement_map:
    abc:
      provider_name: "example_provider"
)";

  // Per-route config with correct requirement_name
  PerRouteConfig per_route;
  per_route.set_requirement_name("abc");
  setup(filter_conf, per_route);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // So the request with a JWT token is OK.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer " + std::string(GoodToken)},
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // A request with missing token is rejected.
  auto response1 = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(response1->complete());
  EXPECT_EQ("401", response1->headers().getStatusValue());
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
