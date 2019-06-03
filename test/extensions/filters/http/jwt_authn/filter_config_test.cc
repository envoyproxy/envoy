#include "common/router/string_accessor_impl.h"
#include "common/stream_info/filter_state_impl.h"

#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::envoy::api::v2::core::Metadata;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

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
  FilterConfig filter_conf(proto_config, "", context);

  StreamInfo::FilterStateImpl filter_state;
  EXPECT_TRUE(filter_conf.findVerifier(
                  Http::TestHeaderMapImpl{
                      {":method", "GET"},
                      {":path", "/path1"},
                  },
                  filter_state) != nullptr);

  EXPECT_TRUE(filter_conf.findVerifier(
                  Http::TestHeaderMapImpl{
                      {":method", "GET"},
                      {":path", "/path2"},
                  },
                  filter_state) == nullptr);
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
  FilterConfig filter_conf(proto_config, "", context);

  // Empty filter_state
  StreamInfo::FilterStateImpl filter_state1;
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), filter_state1) == nullptr);

  // Wrong selector
  StreamInfo::FilterStateImpl filter_state2;
  filter_state2.setData("jwt_selector",
                        std::make_unique<Router::StringAccessorImpl>("wrong_selector"),
                        StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), filter_state2) == nullptr);

  // correct selector
  StreamInfo::FilterStateImpl filter_state3;
  filter_state3.setData("jwt_selector", std::make_unique<Router::StringAccessorImpl>("selector1"),
                        StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), filter_state3) != nullptr);

  // correct selector
  StreamInfo::FilterStateImpl filter_state4;
  filter_state4.setData("jwt_selector", std::make_unique<Router::StringAccessorImpl>("selector2"),
                        StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_TRUE(filter_conf.findVerifier(Http::TestHeaderMapImpl(), filter_state4) != nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
