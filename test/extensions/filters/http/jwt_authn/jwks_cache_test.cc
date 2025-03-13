#include <chrono>
#include <thread>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/jwt_authn/jwks_cache.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"

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
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  JwksCachePtr cache_;
  google::jwt_verify::JwksPtr jwks_;
  MockFunction<Common::JwksFetcherPtr(Upstream::ClusterManager&, const RemoteJwks&)> mock_fetcher_;
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
  context_.server_factory_context_.time_system_.advanceTimeWait(std::chrono::seconds(2));
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

// Test subject constraints for JwtProvider
TEST_F(JwksCacheTest, TestSubjects) {
  setupCache(SubjectConfig);

  {
    auto jwks = cache_->findByIssuer("https://example.com");

    // example.com has a suffix constraint of "@example.com"
    EXPECT_TRUE(jwks->isSubjectAllowed("test@example.com"));
    // Negative test for other subjects
    EXPECT_FALSE(jwks->isSubjectAllowed("othersubject"));
  }

  {
    auto jwks = cache_->findByIssuer("https://spiffe.example.com");

    // Provider has a prefix constraint of spiffe://spiffe.example.com
    EXPECT_TRUE(jwks->isSubjectAllowed("spiffe://spiffe.example.com/service"));
    // Negative test for other subjects
    EXPECT_FALSE(jwks->isSubjectAllowed("spiffe://spiffe.baz.com/service"));
  }

  {
    auto jwks = cache_->findByIssuer("https://nosub.com");

    // Provider no constraints, so test any subject should be allowed
    EXPECT_TRUE(jwks->isSubjectAllowed("any_subject"));
  }

  {
    auto jwks = cache_->findByIssuer("https://regexsub.com");

    // Provider allows spiffe://*.example.com/
    EXPECT_TRUE(jwks->isSubjectAllowed("spiffe://test1.example.com/service"));
    EXPECT_TRUE(jwks->isSubjectAllowed("spiffe://test2.example.com/service"));
    EXPECT_FALSE(jwks->isSubjectAllowed("spiffe://test1.baz.com/service"));
  }
}

// Test lifetime constraints for JwtProvider
TEST_F(JwksCacheTest, TestLifetime) {
  setupCache(ExpirationConfig);

  {
    auto jwks = cache_->findByIssuer("https://example.com");

    absl::Time created;
    absl::Time good_exp = created + absl::Minutes(30);
    absl::Time bad_exp = created + absl::Hours(25);
    // Issuer has a lifetime constraint of 24 hours, so 30 minutes is good.
    EXPECT_TRUE(jwks->isLifetimeAllowed(created, &good_exp));
    // 25 hours should fail based on lifetime
    EXPECT_FALSE(jwks->isLifetimeAllowed(created, &bad_exp));
    // Tokens without expiration should also fail
    EXPECT_FALSE(jwks->isLifetimeAllowed(created, nullptr));
  }

  {
    auto jwks = cache_->findByIssuer("https://spiffe.example.com");

    absl::Time created;
    absl::Time long_exp = created + absl::Hours(2500);
    // Spiffe provider has a infinite constraint, so any time should work.
    EXPECT_TRUE(jwks->isLifetimeAllowed(created, &long_exp));
    // Infinite constraints require an expiration, so null should fail.
    EXPECT_FALSE(jwks->isLifetimeAllowed(created, nullptr));
  }

  {
    auto jwks = cache_->findByIssuer("https://noexp.example.com");

    absl::Time created;
    // Require_expiration set to false, so this should pass.
    EXPECT_TRUE(jwks->isLifetimeAllowed(created, nullptr));
  }
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
