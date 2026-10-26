#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/utility.h"

#include "test/integration/http_integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {
namespace {

// The values that the SDS server serves for the route level OAuth2 configuration. They are
// deliberately different from the static secrets of the listener level configuration so that a
// request proves which configuration, and which secret, was actually used.
constexpr absl::string_view ROUTE_TOKEN_SECRET = "route_token_secret";
constexpr absl::string_view ROUTE_HMAC_SECRET = "route_hmac_secret";

// Verifies that a route configuration that references SDS secrets from a route level OAuth2
// configuration is only published once those secrets have been fetched, i.e. that the route level
// filter configuration is warmed up with the init manager of the route configuration.
//
// The setup is a static listener whose route configuration comes from RDS, with the OAuth2 filter
// in the filter chain:
//  * the listener level OAuth2 configuration uses static secrets, so it is usable immediately;
//  * the route level OAuth2 configuration uses SDS secrets served by a fake SDS server, so it can
//    only be usable after that server has responded.
class OauthRouteWarmingIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  OauthRouteWarmingIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // The xDS cluster stats of this test have no tag extraction rules.
    skip_tag_extraction_rule_check_ = true;
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // The order of the fake upstreams must match the order of the clusters in the bootstrap,
    // because that is the order in which the ports are assigned.
    addFakeUpstream(Http::CodecType::HTTP2); // RDS server (fake_upstreams_[1]).
    addFakeUpstream(Http::CodecType::HTTP2); // SDS server (fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP1); // OAuth2 token endpoint (fake_upstreams_[3]).
  }

  void initialize() override {
    setUpstreamCount(1);
    // The listener cannot be finalized before the initial route configuration is delivered.
    defer_listener_finalization_ = true;

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();

      auto* rds_cluster = static_resources->add_clusters();
      rds_cluster->MergeFrom(static_resources->clusters()[0]);
      rds_cluster->set_name("rds_cluster");
      ConfigHelper::setHttp2(*rds_cluster);

      auto* sds_cluster = static_resources->add_clusters();
      sds_cluster->MergeFrom(static_resources->clusters()[0]);
      sds_cluster->set_name("sds_cluster");
      ConfigHelper::setHttp2(*sds_cluster);

      auto* oauth_cluster = static_resources->add_clusters();
      oauth_cluster->MergeFrom(static_resources->clusters()[0]);
      oauth_cluster->set_name("oauth");

      // The static secrets that are used by the listener level OAuth2 configuration. They are
      // available as soon as the listener is created and never need any warming up.
      auto* token_secret = static_resources->add_secrets();
      token_secret->set_name("listener-token");
      token_secret->mutable_generic_secret()->mutable_secret()->set_inline_string(
          "listener_token_secret");
      auto* hmac_secret = static_resources->add_secrets();
      hmac_secret->set_name("listener-hmac");
      hmac_secret->mutable_generic_secret()->mutable_secret()->set_inline_string(
          "listener_hmac_secret");
    });

    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.oauth2
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2
  config:
    token_endpoint:
      cluster: oauth
      uri: oauth.com/token
      timeout: 3s
    authorization_endpoint: https://listener-oauth.com/oauth/authorize/
    redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
    redirect_path_matcher:
      path:
        exact: /callback
    signout_path:
      path:
        exact: /signout
    credentials:
      client_id: listener-client
      token_secret:
        name: listener-token
      hmac_secret:
        name: listener-hmac
    auth_scopes:
    - user
)EOF");

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* rds = hcm.mutable_rds();
          rds->set_route_config_name(route_config_name_);
          auto* config_source = rds->mutable_config_source();
          config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
          auto* api_config_source = config_source->mutable_api_config_source();
          api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
          api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
          api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
              "rds_cluster");
        });

    HttpIntegrationTest::initialize();
  }

  void TearDown() override {
    closeConnection(rds_connection_);
    closeConnection(sds_connection_);
    closeConnection(oauth_connection_);
    cleanupUpstreamAndDownstream();
  }

  void closeConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  void createRdsStream() {
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, rds_connection_));
    ASSERT_TRUE(rds_connection_->waitForNewStream(*dispatcher_, rds_stream_));
    rds_stream_->startGrpcStream();
  }

  void sendRdsResponse(const std::string& route_config_yaml, const std::string& version) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url(Config::TestTypeUrl::get().RouteConfiguration);
    std::ignore = response.add_resources()->PackFrom(
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(route_config_yaml));
    rds_stream_->sendGrpcMessage(response);
  }

  // Waits for `count` new SDS streams and for the discovery request on each of them, and returns
  // the requested secret names. Each secret of the route level configuration has its own
  // subscription, and so its own stream.
  std::vector<std::string> waitForSdsRequests(size_t count) {
    if (sds_connection_ == nullptr) {
      EXPECT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, sds_connection_));
    }
    std::vector<std::string> requested_secrets;
    for (size_t i = 0; i < count; i++) {
      FakeStreamPtr sds_stream;
      EXPECT_TRUE(sds_connection_->waitForNewStream(*dispatcher_, sds_stream));
      sds_stream->startGrpcStream();

      envoy::service::discovery::v3::DiscoveryRequest request;
      EXPECT_TRUE(sds_stream->waitForGrpcMessage(*dispatcher_, request));
      EXPECT_EQ(1, request.resource_names_size());
      requested_secrets.push_back(request.resource_names(0));
      sds_streams_.push_back(std::move(sds_stream));
    }
    return requested_secrets;
  }

  // Serves the secrets that were requested on the streams collected by waitForSdsRequests().
  void sendSdsResponses(const std::vector<std::string>& requested_secrets,
                        const absl::flat_hash_map<std::string, std::string>& secrets) {
    ASSERT_EQ(requested_secrets.size(), sds_streams_.size());
    for (size_t i = 0; i < requested_secrets.size(); i++) {
      const std::string& name = requested_secrets[i];
      ASSERT_TRUE(secrets.contains(name));

      envoy::extensions::transport_sockets::tls::v3::Secret secret;
      secret.set_name(name);
      secret.mutable_generic_secret()->mutable_secret()->set_inline_string(secrets.at(name));

      envoy::service::discovery::v3::DiscoveryResponse response;
      response.set_version_info("1");
      response.set_type_url(Config::TestTypeUrl::get().Secret);
      std::ignore = response.add_resources()->PackFrom(secret);
      sds_streams_[i]->sendGrpcMessage(response);
    }
  }

  std::string plainRouteConfig() {
    return absl::StrCat(R"EOF(
name: )EOF",
                        route_config_name_, R"EOF(
virtual_hosts:
- name: integration
  domains: ["*"]
  routes:
  - match: {prefix: "/"}
    route: {cluster: cluster_0}
)EOF");
  }

  // The same route configuration, but with a route level OAuth2 configuration whose credentials
  // come from SDS.
  std::string sdsRouteConfig() {
    return absl::StrCat(R"EOF(
name: )EOF",
                        route_config_name_, R"EOF(
virtual_hosts:
- name: integration
  domains: ["*"]
  typed_per_filter_config:
    envoy.filters.http.oauth2:
      "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2PerRoute
      config:
        token_endpoint:
          cluster: oauth
          uri: oauth.com/token
          timeout: 3s
        authorization_endpoint: https://route-oauth.com/oauth/authorize/
        redirect_uri: "%REQ(x-forwarded-proto)%://%REQ(:authority)%/callback"
        redirect_path_matcher:
          path:
            exact: /callback
        signout_path:
          path:
            exact: /signout
        credentials:
          client_id: route-client
          token_secret:
            name: route-token
            sds_config:
              resource_api_version: V3
              api_config_source:
                api_type: GRPC
                transport_api_version: V3
                grpc_services:
                - envoy_grpc:
                    cluster_name: sds_cluster
          hmac_secret:
            name: route-hmac
            sds_config:
              resource_api_version: V3
              api_config_source:
                api_type: GRPC
                transport_api_version: V3
                grpc_services:
                - envoy_grpc:
                    cluster_name: sds_cluster
        auth_scopes:
        - user
  routes:
  - match: {prefix: "/"}
    route: {cluster: cluster_0}
)EOF");
  }

  // Sends an unauthenticated request and returns the redirect that the OAuth2 filter generates.
  IntegrationStreamDecoderPtr sendUnauthenticatedRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                           {":path", "/not/_oauth"},
                                           {":scheme", "http"},
                                           {"x-forwarded-proto", "http"},
                                           {":authority", "authority"}};
    auto response = codec_client_->makeHeaderOnlyRequest(headers);
    EXPECT_TRUE(response->waitForEndStream());
    codec_client_->close();
    return response;
  }

  // Returns the "name=value" pairs of all the Set-Cookie headers of a response, so that they can
  // be sent back on the next request.
  static std::vector<std::string> extractCookies(const Http::ResponseHeaderMap& headers) {
    std::vector<std::string> cookies;
    headers.iterate([&cookies](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      if (absl::EqualsIgnoreCase(header.key().getStringView(), "set-cookie")) {
        const absl::string_view value = header.value().getStringView();
        cookies.emplace_back(value.substr(0, value.find(';')));
      }
      return Http::HeaderMap::Iterate::Continue;
    });
    return cookies;
  }

  std::string configReloadStat() {
    return absl::StrCat("http.config_test.rds.", route_config_name_, ".config_reload");
  }

  const std::string route_config_name_{"dynamic_route"};
  // The secrets that the SDS server serves for the route level configuration.
  const absl::flat_hash_map<std::string, std::string> route_secrets_{
      {"route-token", std::string(ROUTE_TOKEN_SECRET)},
      {"route-hmac", std::string(ROUTE_HMAC_SECRET)}};

  FakeHttpConnectionPtr rds_connection_;
  FakeStreamPtr rds_stream_;
  FakeHttpConnectionPtr sds_connection_;
  std::vector<FakeStreamPtr> sds_streams_;
  FakeHttpConnectionPtr oauth_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, OauthRouteWarmingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(OauthRouteWarmingIntegrationTest, RouteConfigWaitsForSdsSecrets) {
  on_server_init_function_ = [this]() {
    createRdsStream();
    sendRdsResponse(plainRouteConfig(), "1");
  };
  initialize();
  test_server_->waitForCounter(configReloadStat(), Eq(1));
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // The route configuration has no route level OAuth2 configuration yet, so the listener level
  // one is used and the request is redirected to the listener level authorization endpoint.
  auto response = sendUnauthenticatedRequest();
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_TRUE(absl::StartsWith(response->headers().getLocationValue(),
                               "https://listener-oauth.com/oauth/authorize/"));

  // Push a route configuration whose route level OAuth2 configuration references SDS secrets.
  sendRdsResponse(sdsRouteConfig(), "2");

  // Envoy must ask the SDS server for the secrets of the route level configuration.
  const std::vector<std::string> requested_secrets = waitForSdsRequests(2);
  EXPECT_THAT(requested_secrets, testing::UnorderedElementsAre("route-token", "route-hmac"));

  // The new route configuration is warming up and must not be published yet.
  EXPECT_EQ(1, test_server_->counter(configReloadStat())->value());

  // And the previous route configuration is still the one that is used, so the request is still
  // redirected to the listener level authorization endpoint.
  response = sendUnauthenticatedRequest();
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_TRUE(absl::StartsWith(response->headers().getLocationValue(),
                               "https://listener-oauth.com/oauth/authorize/"));

  // Serve the secrets that the route level configuration is waiting for.
  sendSdsResponses(requested_secrets, route_secrets_);

  // Only now, once the secrets are available, the route configuration is published.
  test_server_->waitForCounter(configReloadStat(), Eq(2));

  // The route level OAuth2 configuration is in effect: the request is redirected to the route
  // level authorization endpoint with the route level client id.
  response = sendUnauthenticatedRequest();
  EXPECT_EQ("302", response->headers().getStatusValue());
  const absl::string_view location = response->headers().getLocationValue();
  EXPECT_TRUE(absl::StartsWith(location, "https://route-oauth.com/oauth/authorize/"));
  EXPECT_TRUE(absl::StrContains(location, "client_id=route-client"));
}

// Verifies that the SDS secrets of a route level OAuth2 configuration are not only fetched before
// the route configuration is published, but are also the ones that the filter uses: the OAuth2
// flow of the route must be driven with the client secret that the SDS server served.
TEST_P(OauthRouteWarmingIntegrationTest, RouteLevelSdsSecretsAreUsedForTokenExchange) {
  on_server_init_function_ = [this]() {
    createRdsStream();
    sendRdsResponse(sdsRouteConfig(), "1");
  };
  initialize();

  // The very first route configuration also waits for the secrets of its route level OAuth2
  // configuration before it is published, so the listener is still warming up here.
  const std::vector<std::string> requested_secrets = waitForSdsRequests(2);
  ASSERT_NE(nullptr, test_server_->counter(configReloadStat()));
  EXPECT_EQ(0, test_server_->counter(configReloadStat())->value());

  sendSdsResponses(requested_secrets, route_secrets_);
  test_server_->waitForCounter(configReloadStat(), Eq(1));
  test_server_->waitUntilListenersReady();
  registerTestServerPorts({"http"});

  // Step 1: an unauthenticated request is redirected to the route level authorization endpoint.
  // The cookies of that redirect are signed and encrypted with the HMAC secret from SDS.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/not/_oauth"},
                                     {":scheme", "http"},
                                     {"x-forwarded-proto", "http"},
                                     {":authority", "authority"}});
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ("302", response->headers().getStatusValue());
  const std::string location(response->headers().getLocationValue());
  ASSERT_TRUE(absl::StartsWith(location, "https://route-oauth.com/oauth/authorize/"));
  const std::vector<std::string> cookies = extractCookies(response->headers());
  // The state is not decoded here, so that it can be sent back as is on the callback request.
  const std::string state =
      Http::Utility::QueryParamsMulti::parseQueryString(location).getFirstValue("state").value_or(
          "");
  ASSERT_FALSE(state.empty());
  codec_client_->close();

  // Step 2: the client comes back to the redirect path with those cookies and the authorization
  // code.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl callback_headers{
      {":method", "GET"},
      {":path", absl::StrCat("/callback?code=foo&state=", state)},
      {":scheme", "http"},
      {"x-forwarded-proto", "http"},
      {":authority", "authority"}};
  for (const auto& cookie : cookies) {
    callback_headers.addCopy(Http::Headers::get().Cookie, cookie);
  }
  auto callback_response = codec_client_->makeHeaderOnlyRequest(callback_headers);

  // The filter exchanges the code at the token endpoint of the route level configuration, using
  // the client secret that the SDS server served for this route.
  ASSERT_TRUE(fake_upstreams_[3]->waitForHttpConnection(*dispatcher_, oauth_connection_));
  FakeStreamPtr token_request;
  ASSERT_TRUE(oauth_connection_->waitForNewStream(*dispatcher_, token_request));
  ASSERT_TRUE(token_request->waitForEndStream(*dispatcher_));
  const auto token_request_params =
      Http::Utility::QueryParamsMulti::parseParameters(token_request->body().toString(), 0, true);
  EXPECT_EQ("route-client", token_request_params.getFirstValue("client_id").value_or(""));
  EXPECT_EQ(ROUTE_TOKEN_SECRET, token_request_params.getFirstValue("client_secret").value_or(""));

  token_request->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  Buffer::OwnedImpl token_response_body(
      R"EOF({"access_token":"bar","expires_in":600,"refresh_token":"foo"})EOF");
  token_request->encodeData(token_response_body, true);

  // And the client is redirected back to the URL it originally asked for.
  ASSERT_TRUE(callback_response->waitForEndStream());
  EXPECT_EQ("302", callback_response->headers().getStatusValue());
  EXPECT_EQ("http://authority/not/_oauth", callback_response->headers().getLocationValue());
  codec_client_->close();
}

} // namespace
} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
