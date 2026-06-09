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
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://example_service");

  auto token = std::make_unique<GcpToken>();
  token->token = "foo";
  token->expires_at = ExpTime;
  token->audience = audience;

  token_cache_->insert(std::move(token));
  auto found_token = token_cache_->lookUp(audience, absl::nullopt);
  EXPECT_TRUE(found_token.has_value());
  EXPECT_EQ(found_token.value(), "foo");
}

TEST_F(TokenCacheTest, ExpiredToken) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://example_service");

  auto token = std::make_unique<GcpToken>();
  token->token = "foo";
  token->expires_at = 1205005587; // Sat Mar 08 2008 14:46:27 GMT-0500
  token->audience = audience;

  token_cache_->insert(std::move(token));
  auto found_token = token_cache_->lookUp(audience, absl::nullopt);
  EXPECT_FALSE(found_token.has_value());
}

// Test the token with the clock skew.
TEST_F(TokenCacheTest, TokenWithClockSkew) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://example_service");

  // Set the time to exp_time + 1s.
  // i.e., The expiration time in the token is Sun May 29 2033 13:36:41 GMT and the time we
  // set to is Sun May 29 2033 13:35:42 GMT.
  const time_t exp_time_with_skew = ExpTime - JwtVerify::kClockSkewInSecond;
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(exp_time_with_skew + 1));

  auto token = std::make_unique<GcpToken>();
  token->token = "foo";
  token->expires_at = ExpTime;
  token->audience = audience;

  token_cache_->insert(std::move(token));
  auto found_token = token_cache_->lookUp(audience, absl::nullopt);
  EXPECT_FALSE(found_token.has_value());

  // Set the time to exp_time - 1s.
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(exp_time_with_skew));
  auto token2 = std::make_unique<GcpToken>();
  token2->token = "foo";
  token2->expires_at = ExpTime;
  token2->audience = audience;

  token_cache_->insert(std::move(token2));
  found_token = token_cache_->lookUp(audience, absl::nullopt);
  EXPECT_TRUE(found_token.has_value());
  EXPECT_EQ(found_token.value(), "foo");
}

TEST_F(TokenCacheTest, TokenWithoutExpiration) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.set_url("http://example_service");

  auto token = std::make_unique<GcpToken>();
  token->token = "foo";
  token->expires_at = 0; // 0 indicates no expiration
  token->audience = audience;

  token_cache_->insert(std::move(token));

  // Advance the simulated clock by a huge amount (e.g. 10 years)
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(ExpTime + 315360000));

  // The lookup should still find the token since it has no expiration!
  auto found_token = token_cache_->lookUp(audience, absl::nullopt);
  EXPECT_TRUE(found_token.has_value());
  EXPECT_EQ(found_token.value(), "foo");
}

TEST_F(TokenCacheTest, LruEviction) {
  // Initialize a cache with capacity 2.
  envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig config;
  config.mutable_cache_size()->set_value(2);
  auto small_cache = std::make_unique<TokenCacheImpl>(config, time_system_);

  envoy::extensions::filters::http::gcp_authn::v3::Audience req1;
  req1.set_url("http://service1");
  envoy::extensions::filters::http::gcp_authn::v3::Audience req2;
  req2.set_url("http://service2");
  envoy::extensions::filters::http::gcp_authn::v3::Audience req3;
  req3.set_url("http://service3");

  // Insert first two entries.
  auto t1 = std::make_unique<GcpToken>();
  t1->token = "token1";
  t1->expires_at = ExpTime;
  t1->audience = req1;
  small_cache->insert(std::move(t1));

  auto t2 = std::make_unique<GcpToken>();
  t2->token = "token2";
  t2->expires_at = ExpTime;
  t2->audience = req2;
  small_cache->insert(std::move(t2));

  // Verify both are present.
  EXPECT_TRUE(small_cache->lookUp(req1, absl::nullopt).has_value());
  EXPECT_TRUE(small_cache->lookUp(req2, absl::nullopt).has_value());

  // Insert third entry, which should trigger eviction of the least recently used entry (req1).
  auto t3 = std::make_unique<GcpToken>();
  t3->token = "token3";
  t3->expires_at = ExpTime;
  t3->audience = req3;
  small_cache->insert(std::move(t3));

  // Verify req1 is evicted, while req2 and req3 are still present.
  EXPECT_FALSE(small_cache->lookUp(req1, absl::nullopt).has_value());
  EXPECT_TRUE(small_cache->lookUp(req2, absl::nullopt).has_value());
  EXPECT_TRUE(small_cache->lookUp(req3, absl::nullopt).has_value());
}

TEST_F(TokenCacheTest, BoundJwtFingerprintAwareCache) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_bound_jwt()->set_url("http://bound_service");

  // Insert token for certificate fingerprint A.
  auto tokenA = std::make_unique<GcpToken>("token_for_A", ExpTime, audience, "fingerprint_A");
  token_cache_->insert(std::move(tokenA));

  // Lookup with fingerprint A should succeed and return "token_for_A".
  auto foundA = token_cache_->lookUp(audience, "fingerprint_A");
  EXPECT_TRUE(foundA.has_value());
  EXPECT_EQ(foundA.value(), "token_for_A");

  // Lookup with fingerprint B should result in a cache miss (return nullopt).
  auto foundB = token_cache_->lookUp(audience, "fingerprint_B");
  EXPECT_FALSE(foundB.has_value());

  // Insert token for certificate fingerprint B.
  auto tokenB = std::make_unique<GcpToken>("token_for_B", ExpTime, audience, "fingerprint_B");
  token_cache_->insert(std::move(tokenB));

  // Lookup with B should now succeed and return "token_for_B".
  foundB = token_cache_->lookUp(audience, "fingerprint_B");
  EXPECT_TRUE(foundB.has_value());
  EXPECT_EQ(foundB.value(), "token_for_B");

  // Lookup with A should still succeed and return "token_for_A" (no collision/overwriting!).
  foundA = token_cache_->lookUp(audience, "fingerprint_A");
  EXPECT_TRUE(foundA.has_value());
  EXPECT_EQ(foundA.value(), "token_for_A");
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
