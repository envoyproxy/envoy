#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/router/string_accessor_impl.h"
#include "common/stream_info/filter_state_impl.h"

#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using testing::ReturnRef;

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
  auto filter_conf = FilterConfigImpl::create(proto_config, "", context);

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

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  // Make sure that the thread callbacks are not invoked inline.
  server_context.thread_local_.defer_data = true;
  {
    // Scope in all the things that the filter depends on, so they are destroyed as we leave the
    // scope.
    NiceMock<Server::Configuration::MockFactoryContext> context;
    // The threadLocal, dispatcher and api that are used by the filter config, actually belong to
    // the server factory context that who's lifetime is longer. We simulate that by returning
    // their instances from outside the scope.
    ON_CALL(context, dispatcher()).WillByDefault(ReturnRef(server_context.dispatcher()));
    ON_CALL(context, api()).WillByDefault(ReturnRef(server_context.api()));
    ON_CALL(context, threadLocal()).WillByDefault(ReturnRef(server_context.threadLocal()));

    JwtAuthentication proto_config;
    TestUtility::loadFromYaml(config, proto_config);
    auto filter_conf = FilterConfigImpl::create(proto_config, "", context);
  }

  // Even though filter_conf is now de-allocated, using a reference to it might still work, as its
  // memory was not cleared. This leads to a false positive in this test when run normally. The
  // test should fail under asan if the code uses invalid reference.

  // Make sure the filter scheduled a callback
  EXPECT_EQ(1, server_context.thread_local_.deferred_data_.size());

  // Simulate a situation where the callback is called after the filter config is destroyed.
  // call the tls callback. we want to make sure that it doesn't depend on objects
  // that are out of scope.
  EXPECT_NO_THROW(server_context.thread_local_.call());
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
  auto filter_conf = FilterConfigImpl::create(proto_config, "", context);

  // Empty filter_state
  StreamInfo::FilterStateImpl filter_state1(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state1) ==
              nullptr);

  // Wrong selector
  StreamInfo::FilterStateImpl filter_state2(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state2.setData(
      "jwt_selector", std::make_unique<Router::StringAccessorImpl>("wrong_selector"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state2) ==
              nullptr);

  // correct selector
  StreamInfo::FilterStateImpl filter_state3(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state3.setData("jwt_selector", std::make_unique<Router::StringAccessorImpl>("selector1"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state3) !=
              nullptr);

  // correct selector
  StreamInfo::FilterStateImpl filter_state4(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state4.setData("jwt_selector", std::make_unique<Router::StringAccessorImpl>("selector2"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_TRUE(filter_conf->findVerifier(Http::TestRequestHeaderMapImpl(), filter_state4) !=
              nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
