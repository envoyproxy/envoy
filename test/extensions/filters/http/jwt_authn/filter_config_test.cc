#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig;
using testing::HasSubstr;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

using StatusHelpers::HasStatus;

TEST(HttpJwtAuthnFilterConfigTest, FindByMatch) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
rules:
- match:
    path: /path1
  requires:
    provider_name: provider1
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());

  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(
                  Http::TestRequestHeaderMapImpl{
                      {":method", "GET"},
                      {":path", "/path1"},
                  },
                  filter_state) != nullptr);

  EXPECT_TRUE(filter_conf->findVerifier(
                  Http::TestRequestHeaderMapImpl{
                      {":method", "GET"},
                      {":path", "/path2"},
                  },
                  filter_state) == nullptr);
}

TEST(HttpJwtAuthnFilterConfigTest, FindByMatchDisabled) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
rules:
- match:
    path: /path1
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());

  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(
                  Http::TestRequestHeaderMapImpl{
                      {":path", "/path1"},
                  },
                  filter_state) == nullptr);
}

TEST(HttpJwtAuthnFilterConfigTest, FindByMatchWrongRequirementName) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
rules:
- match:
    path: /path1
  requirement_name: rr
requirement_map:
  r1:
    provider_name: provider1
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  FilterConfigImpl filter_config(proto_config, "", context, creation_status);
  EXPECT_THAT(creation_status, HasStatus(absl::StatusCode::kInvalidArgument,
                                         "Wrong requirement_name: rr. It should be one of [r1]"));
}

TEST(HttpJwtAuthnFilterConfigTest, FindByMatchRequirementName) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
  provider2:
    issuer: issuer2
    local_jwks:
      inline_string: jwks
rules:
- match:
    path: /path1
  requirement_name: r1
- match:
    path: /path2
  requirement_name: r2
requirement_map:
  r1:
    provider_name: provider1
  r2:
    provider_name: provider2
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);

  EXPECT_TRUE(filter_conf->findVerifier(
                  Http::TestRequestHeaderMapImpl{
                      {":path", "/path1"},
                  },
                  filter_state) != nullptr);
  EXPECT_TRUE(filter_conf->findVerifier(
                  Http::TestRequestHeaderMapImpl{
                      {":path", "/path2"},
                  },
                  filter_state) != nullptr);
}

TEST(HttpJwtAuthnFilterConfigTest, VerifyTLSLifetime) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
rules:
- match:
    path: /path1
  requires:
    provider_name: provider1
)";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.thread_local_.defer_data_ = true;

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());

  // Even though filter_conf is now de-allocated, using a reference to it might still work, as its
  // memory was not cleared. This leads to a false positive in this test when run normally. The
  // test should fail under asan if the code uses invalid reference.

  // Make sure the filter scheduled a callback
  EXPECT_EQ(1, context.server_factory_context_.thread_local_.deferred_data_.size());

  // Simulate a situation where the callback is called after the filter config is destroyed.
  // call the tls callback. we want to make sure that it doesn't depend on objects
  // that are out of scope.
  EXPECT_NO_THROW(context.server_factory_context_.thread_local_.call());
}

TEST(HttpJwtAuthnFilterConfigTest, FindByFilterState) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
  provider2:
    issuer: issuer2
    local_jwks:
      inline_string: jwks
filter_state_rules:
  name: jwt_selector
  requires:
    selector1:
      provider_name: provider1
    selector2:
      provider_name: provider2
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());

  // Empty filter_state
  StreamInfo::FilterStateImpl filter_state1(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state1) ==
              nullptr);

  // Wrong selector
  StreamInfo::FilterStateImpl filter_state2(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state2.setData("jwt_selector",
                        std::make_unique<Router::StringAccessorImpl>("wrong_selector"),
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state2) ==
              nullptr);

  // correct selector
  StreamInfo::FilterStateImpl filter_state3(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state3.setData("jwt_selector", std::make_unique<Router::StringAccessorImpl>("selector1"),
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state3) !=
              nullptr);

  // correct selector
  StreamInfo::FilterStateImpl filter_state4(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state4.setData("jwt_selector", std::make_unique<Router::StringAccessorImpl>("selector2"),
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state4) !=
              nullptr);
}

TEST(HttpJwtAuthnFilterConfigTest, FindByRequiremenMap) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    local_jwks:
      inline_string: jwks
  provider2:
    issuer: issuer2
    local_jwks:
      inline_string: jwks
requirement_map:
  r1:
    provider_name: provider1
  r2:
    provider_name: provider2
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());

  PerRouteConfig per_route;
  const Verifier* verifier;
  std::string error_msg;

  per_route.Clear();
  per_route.set_disabled(true);
  std::tie(verifier, error_msg) =
      filter_conf->findPerRouteVerifier(PerRouteFilterConfig(per_route));
  EXPECT_EQ(verifier, nullptr);
  EXPECT_EQ(error_msg, EMPTY_STRING);

  per_route.Clear();
  per_route.set_requirement_name("r1");
  std::tie(verifier, error_msg) =
      filter_conf->findPerRouteVerifier(PerRouteFilterConfig(per_route));
  EXPECT_NE(verifier, nullptr);
  EXPECT_EQ(error_msg, EMPTY_STRING);

  per_route.Clear();
  per_route.set_requirement_name("r2");
  std::tie(verifier, error_msg) =
      filter_conf->findPerRouteVerifier(PerRouteFilterConfig(per_route));
  EXPECT_NE(verifier, nullptr);
  EXPECT_EQ(error_msg, EMPTY_STRING);

  per_route.Clear();
  per_route.set_requirement_name("wrong-name");
  std::tie(verifier, error_msg) =
      filter_conf->findPerRouteVerifier(PerRouteFilterConfig(per_route));
  EXPECT_EQ(verifier, nullptr);
  EXPECT_EQ(error_msg, "Wrong requirement_name: wrong-name. It should be one of [r1,r2]");
}

TEST(HttpJwtAuthnFilterConfigTest, RemoteJwksDurationVeryBig) {
  // remote_jwks.duration.seconds should be less than half of:
  // 9223372036 = max_int64 / 1e9, which is about 300 years.
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    remote_jwks:
      cache_duration:
        seconds: 5223372036
      http_uri:
        uri: http://www.google.com
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  FilterConfigImpl filter_config(proto_config, "", context, creation_status);
  EXPECT_THAT(creation_status,
              HasStatus(absl::StatusCode::kOutOfRange, HasSubstr("Duration out-of-range")));
}

TEST(HttpJwtAuthnFilterConfigTest, RemoteJwksInvalidUri) {
  // Invalid URI should fail config validation.
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    remote_jwks:
      http_uri:
        uri: http://www.not\nvalid.com
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  FilterConfigImpl filter_config(proto_config, "", context, creation_status);
  EXPECT_THAT(creation_status,
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("invalid URI")));
}

TEST(HttpJwtAuthnFilterConfigTest, RemoteJwksValidUri) {
  // Valid URI should not fail config validation.
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    remote_jwks:
      http_uri:
        uri: http://www.valid.com/resource
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  FilterConfigImpl filter_config(proto_config, "", context, creation_status);
  EXPECT_TRUE(creation_status.ok());
}

TEST(HttpJwtAuthnFilterConfigTest, RemoteJwksAsyncFetchRefetchDurationVeryBig) {
  // failed_refetch_duration.duration.seconds should be less than:
  // 9223372036 = max_int64 / 1e9, which is about 300 years.
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    remote_jwks:
      async_fetch:
        failed_refetch_duration:
          seconds: 9223372136
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  FilterConfigImpl filter_config(proto_config, "", context, creation_status);
  EXPECT_THAT(creation_status,
              HasStatus(absl::StatusCode::kOutOfRange, HasSubstr("Duration out-of-range")));
}

TEST(HttpJwtAuthnFilterConfigTest, RemoteJwksWithRetryPolicy) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    remote_jwks:
      http_uri:
        uri: http://www.valid.com/resource
        cluster: pubkey_cluster
        timeout: 1s
      retry_policy:
        retry_back_off:
          base_interval: 1s
          max_interval: 10s
        num_retries: 5
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  auto filter_conf = std::make_unique<FilterConfigImpl>(proto_config, "", context, creation_status);
  ASSERT_TRUE(creation_status.ok());
  auto* jwks_data = filter_conf->getJwksCache().findByIssuer("issuer1");
  EXPECT_NE(nullptr, jwks_data);
  EXPECT_NE(nullptr, jwks_data->retryPolicy());
  EXPECT_EQ(5, jwks_data->retryPolicy()->numRetries());
}

TEST(HttpJwtAuthnFilterConfigTest, RemoteJwksWithInvalidRetryPolicy) {
  const char config[] = R"(
providers:
  provider1:
    issuer: issuer1
    remote_jwks:
      http_uri:
        uri: http://www.valid.com/resource
        cluster: pubkey_cluster
        timeout: 1s
      retry_policy:
        retry_back_off:
          base_interval: 10s
          max_interval: 1s
)";

  JwtAuthentication proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::Status creation_status = absl::OkStatus();
  FilterConfigImpl filter(proto_config, "", context, creation_status);
  EXPECT_THAT(
      creation_status,
      HasStatus(absl::StatusCode::kInvalidArgument,
                HasSubstr("max_interval must be greater than or equal to the base_interval")));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
