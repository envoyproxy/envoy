#include <cstdint>
#include <memory>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/gcp_authn/token_cache.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using Server::Configuration::MockFactoryContext;
using ::testing::NiceMock;

const time_t ExpTime = 2001001001;

const char DefaultConfig[] = R"EOF(
    http_uri:
      uri: "gcp_authn:9000"
      cluster: gcp_authn
      timeout:
        seconds: 5
    cache_config:
      cache_size: 100
  )EOF";

class TokenCacheTest : public testing::Test {
public:
  TokenCacheTest() {
    envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig config;
    // Initialize the token cache with filter configuration.
    TestUtility::loadFromYaml(DefaultConfig, config);
    token_cache_ = std::make_unique<TokenCacheImpl>(config.cache_config(), time_system_);
  }

  NiceMock<MockFactoryContext> context_;
  std::unique_ptr<TokenCacheImpl> token_cache_;
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(TokenCacheTest, ValidToken) {
  EXPECT_EQ(token_cache_->capacity(), 100);
  envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest token_request;
  token_request.mutable_jwt()->set_audience("http://example_service");

  auto token = std::make_unique<GcpToken>();
  token->token_ = "foo";
  token->expires_at_ = ExpTime;

  token_cache_->insert(token_request, std::move(token));
  auto found_token = token_cache_->lookUp(token_request);
  EXPECT_TRUE(found_token.has_value());
  EXPECT_EQ(found_token.value(), "foo");
}

TEST_F(TokenCacheTest, ExpiredToken) {
  envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest token_request;
  token_request.mutable_jwt()->set_audience("http://example_service");

  auto token = std::make_unique<GcpToken>();
  token->token_ = "foo";
  token->expires_at_ = 1205005587; // Sat Mar 08 2008 14:46:27 GMT-0500

  token_cache_->insert(token_request, std::move(token));
  auto found_token = token_cache_->lookUp(token_request);
  EXPECT_FALSE(found_token.has_value());
}

// Test the token with the clock skew.
TEST_F(TokenCacheTest, TokenWithClockSkew) {
  envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest token_request;
  token_request.mutable_jwt()->set_audience("http://example_service");

  // Set the time to exp_time + 1s.
  // i.e., The expiration time in the token is Sun May 29 2033 13:36:41 GMT and the time we
  // set to is Sun May 29 2033 13:35:42 GMT.
  const time_t exp_time_with_skew = ExpTime - JwtVerify::kClockSkewInSecond;
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(exp_time_with_skew + 1));

  auto token = std::make_unique<GcpToken>();
  token->token_ = "foo";
  token->expires_at_ = ExpTime;

  token_cache_->insert(token_request, std::move(token));
  auto found_token = token_cache_->lookUp(token_request);
  EXPECT_FALSE(found_token.has_value());

  // Set the time to exp_time - 1s.
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(exp_time_with_skew));
  auto token2 = std::make_unique<GcpToken>();
  token2->token_ = "foo";
  token2->expires_at_ = ExpTime;

  token_cache_->insert(token_request, std::move(token2));
  found_token = token_cache_->lookUp(token_request);
  EXPECT_TRUE(found_token.has_value());
  EXPECT_EQ(found_token.value(), "foo");
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
