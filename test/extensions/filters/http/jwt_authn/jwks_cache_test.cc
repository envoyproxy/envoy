#include <chrono>
#include <thread>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/protobuf/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class JwksCacheTest : public testing::Test {
protected:
  JwksCacheTest() : api_(Api::createApiForTest()) {}
  void SetUp() override {
    TestUtility::loadFromYaml(ExampleConfig, config_);
    cache_ = JwksCache::create(config_, time_system_, *api_);
    jwks_ = google::jwt_verify::Jwks::createFrom(PublicKey, google::jwt_verify::Jwks::JWKS);
  }

  Event::SimulatedTimeSystem time_system_;
  JwtAuthentication config_;
  JwksCachePtr cache_;
  google::jwt_verify::JwksPtr jwks_;
  Api::ApiPtr api_;
};

// Test findByIssuer
TEST_F(JwksCacheTest, TestFindByIssuer) {
  EXPECT_TRUE(cache_->findByIssuer("https://example.com") != nullptr);
  EXPECT_TRUE(cache_->findByIssuer("other-issuer") == nullptr);
}

// Test setRemoteJwks and its expiration
TEST_F(JwksCacheTest, TestSetRemoteJwks) {
  auto& provider0 = (*config_.mutable_providers())[std::string(ProviderName)];
  // Set cache_duration to 1 second to test expiration
  provider0.mutable_remote_jwks()->mutable_cache_duration()->set_seconds(1);
  cache_ = JwksCache::create(config_, time_system_, *api_);

  auto jwks = cache_->findByIssuer("https://example.com");
  EXPECT_TRUE(jwks->getJwksObj() == nullptr);

  EXPECT_EQ(jwks->setRemoteJwks(std::move(jwks_))->getStatus(), Status::Ok);
  EXPECT_FALSE(jwks->getJwksObj() == nullptr);
  EXPECT_FALSE(jwks->isExpired());

  // cache duration is 1 second, sleep two seconds to expire it
  time_system_.sleep(std::chrono::seconds(2));
  EXPECT_TRUE(jwks->isExpired());
}

// Test setRemoteJwks and use default cache duration.
TEST_F(JwksCacheTest, TestSetRemoteJwksWithDefaultCacheDuration) {
  auto& provider0 = (*config_.mutable_providers())[std::string(ProviderName)];
  // Clear cache_duration to use default one.
  provider0.mutable_remote_jwks()->clear_cache_duration();
  cache_ = JwksCache::create(config_, time_system_, *api_);

  auto jwks = cache_->findByIssuer("https://example.com");
  EXPECT_TRUE(jwks->getJwksObj() == nullptr);

  EXPECT_EQ(jwks->setRemoteJwks(std::move(jwks_))->getStatus(), Status::Ok);
  EXPECT_FALSE(jwks->getJwksObj() == nullptr);
  EXPECT_FALSE(jwks->isExpired());
}

// Test a good local jwks
TEST_F(JwksCacheTest, TestGoodInlineJwks) {
  auto& provider0 = (*config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_remote_jwks();
  auto local_jwks = provider0.mutable_local_jwks();
  local_jwks->set_inline_string(PublicKey);

  cache_ = JwksCache::create(config_, time_system_, *api_);

  auto jwks = cache_->findByIssuer("https://example.com");
  EXPECT_FALSE(jwks->getJwksObj() == nullptr);
  EXPECT_FALSE(jwks->isExpired());
}

// Test a bad local jwks
TEST_F(JwksCacheTest, TestBadInlineJwks) {
  auto& provider0 = (*config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_remote_jwks();
  auto local_jwks = provider0.mutable_local_jwks();
  local_jwks->set_inline_string("BAD-JWKS");

  cache_ = JwksCache::create(config_, time_system_, *api_);

  auto jwks = cache_->findByIssuer("https://example.com");
  EXPECT_TRUE(jwks->getJwksObj() == nullptr);
}

// Test audiences with different formats
TEST_F(JwksCacheTest, TestAudiences) {
  auto jwks = cache_->findByIssuer("https://example.com");

  /**
   * when comparing audiences, protocol scheme and trailing slash
   * should be sanitized.
   * In this test, jwks config has following:
   *
   * audiences:
   * - example_service
   * - http://example_service1
   * - https://example_service2/
   *
   */

  // incoming has http://, config doesn't
  EXPECT_TRUE(jwks->areAudiencesAllowed({"http://example_service/"}));

  // incoming has https://, config is http://
  // incoming has tailing slash, config has not tailing slash
  EXPECT_TRUE(jwks->areAudiencesAllowed({"https://example_service1/"}));

  // incoming without tailing slash, config has tailing slash
  // incoming has http://, config is https://
  EXPECT_TRUE(jwks->areAudiencesAllowed({"http://example_service2"}));

  // Multiple audiences: a good one and a wrong one
  EXPECT_TRUE(jwks->areAudiencesAllowed({"example_service", "wrong-audience"}));

  // Wrong multiple audiences
  EXPECT_FALSE(jwks->areAudiencesAllowed({"wrong-audience1", "wrong-audience2"}));
}

// Test findByProvider
TEST_F(JwksCacheTest, TestFindByProvider) {
  EXPECT_TRUE(cache_->findByProvider(ProviderName) != nullptr);
  EXPECT_TRUE(cache_->findByProvider("other-provider") == nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
