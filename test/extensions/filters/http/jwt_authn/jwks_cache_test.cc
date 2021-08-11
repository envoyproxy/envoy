#include <chrono>
#include <thread>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/jwt_authn/jwks_cache.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;
using ::google::jwt_verify::Status;
using ::testing::MockFunction;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

JwtAuthnFilterStats generateMockStats(Stats::Scope& scope) {
  return {ALL_JWT_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, ""))};
}

class JwksCacheTest : public testing::Test {
protected:
  JwksCacheTest() : stats_(generateMockStats(context_.scope())) {}

  void SetUp() override {
    // fetcher is only called at async_fetch. In this test, it is never called.
    EXPECT_CALL(mock_fetcher_, Call(_, _)).Times(0);
    setupCache(ExampleConfig);
    jwks_ = google::jwt_verify::Jwks::createFrom(PublicKey, google::jwt_verify::Jwks::JWKS);
  }

  void setupCache(const std::string& config_str) {
    TestUtility::loadFromYaml(config_str, config_);
    cache_ = JwksCache::create(config_, context_, mock_fetcher_.AsStdFunction(), stats_);
  }

  JwtAuthentication config_;
  JwksCachePtr cache_;
  google::jwt_verify::JwksPtr jwks_;
  MockFunction<Common::JwksFetcherPtr(Upstream::ClusterManager&, const RemoteJwks&)> mock_fetcher_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  JwtAuthnFilterStats stats_;
};

// Test findByProvider
TEST_F(JwksCacheTest, TestFindByProvider) {
  EXPECT_TRUE(cache_->findByProvider(ProviderName) != nullptr);
}

// Test findByIssuer
TEST_F(JwksCacheTest, TestFindByIssuer) {
  EXPECT_TRUE(cache_->findByIssuer("https://example.com") != nullptr);
  EXPECT_TRUE(cache_->findByIssuer("other-issuer") == nullptr);
}

// Test findByIssuer with issuer not specified.
TEST_F(JwksCacheTest, TestEmptyIssuer) {
  setupCache(R"(
providers:
  provider1:
    forward_payload_header: jwt-payload1
  provider2:
    issuer: issuer2
    forward_payload_header: jwt-payload2
)");

  // provider1 doesn't have "issuer" specified, any non-specified issuers,
  // including empty issuer can use it.
  auto* data1 = cache_->findByIssuer("abcd");
  auto* data_empty = cache_->findByIssuer("");
  EXPECT_TRUE(data1 != nullptr);
  EXPECT_TRUE(data_empty == data1);
  EXPECT_EQ(data1->getJwtProvider().forward_payload_header(), "jwt-payload1");

  // provider2 has "issuer" specified, only its specified issuer can use it.
  auto* data2 = cache_->findByIssuer("issuer2");
  EXPECT_TRUE(data2 != nullptr);
  EXPECT_EQ(data2->getJwtProvider().forward_payload_header(), "jwt-payload2");
}

// Test setRemoteJwks and its expiration
TEST_F(JwksCacheTest, TestSetRemoteJwks) {
  auto& provider0 = (*config_.mutable_providers())[std::string(ProviderName)];
  // Set cache_duration to 1 second to test expiration
  provider0.mutable_remote_jwks()->mutable_cache_duration()->set_seconds(1);
  cache_ = JwksCache::create(config_, context_, mock_fetcher_.AsStdFunction(), stats_);

  auto jwks = cache_->findByIssuer("https://example.com");
  EXPECT_TRUE(jwks->getJwksObj() == nullptr);

  EXPECT_EQ(jwks->setRemoteJwks(std::move(jwks_))->getStatus(), Status::Ok);
  EXPECT_FALSE(jwks->getJwksObj() == nullptr);
  EXPECT_FALSE(jwks->isExpired());

  // cache duration is 1 second, sleep two seconds to expire it
  context_.time_system_.advanceTimeWait(std::chrono::seconds(2));
  EXPECT_TRUE(jwks->isExpired());
}

// Test setRemoteJwks and use default cache duration.
TEST_F(JwksCacheTest, TestSetRemoteJwksWithDefaultCacheDuration) {
  auto& provider0 = (*config_.mutable_providers())[std::string(ProviderName)];
  // Clear cache_duration to use default one.
  provider0.mutable_remote_jwks()->clear_cache_duration();
  cache_ = JwksCache::create(config_, context_, mock_fetcher_.AsStdFunction(), stats_);

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

  cache_ = JwksCache::create(config_, context_, mock_fetcher_.AsStdFunction(), stats_);

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

  cache_ = JwksCache::create(config_, context_, mock_fetcher_.AsStdFunction(), stats_);

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

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
