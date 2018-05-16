#include <chrono>
#include <thread>

#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include "test/test_common/utility.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

const char ExampleConfig[] = R"(
rules:
  - issuer: issuer1
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      cache_duration:
        seconds: 1
)";

// A good public key
const std::string PublicKey =
    "{\"keys\": [{"
    "  \"kty\": \"RSA\","
    "  \"n\": "
    "\"up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1q"
    "mUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrk"
    "U7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaE"
    "WopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_Zdb"
    "oY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw\","
    "  \"e\": \"AQAB\","
    "  \"alg\": \"RS256\","
    "  \"kid\": \"62a93512c9ee4c7f8067b5a216dade2763d32a47\""
    "},"
    "{"
    "  \"kty\": \"RSA\","
    "  \"n\": "
    "\"up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1q"
    "mUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrk"
    "U7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaE"
    "WopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_Zdb"
    "oY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw\","
    "  \"e\": \"AQAB\","
    "  \"alg\": \"RS256\","
    "  \"kid\": \"b3319a147514df7ee5e4bcdee51350cc890cc89e\""
    "}]}";

class JwksCacheTest : public ::testing::Test {
public:
  void SetUp() {
    MessageUtil::loadFromYaml(ExampleConfig, config_);
    cache_ = JwksCache::create(config_);
  }

  JwtAuthentication config_;
  JwksCachePtr cache_;
};

// Test findByIssuer
TEST_F(JwksCacheTest, TestFindByIssuer) {
  EXPECT_TRUE(cache_->findByIssuer("issuer1") != nullptr);
  EXPECT_TRUE(cache_->findByIssuer("issuer2") == nullptr);
}

// Test setRemoteJwks and its expiration
TEST_F(JwksCacheTest, TestSetRemoteJwks) {
  auto jwks = cache_->findByIssuer("issuer1");
  EXPECT_TRUE(jwks->getJwksObj() == nullptr);

  EXPECT_EQ(jwks->setRemoteJwks(PublicKey), Status::Ok);
  EXPECT_FALSE(jwks->getJwksObj() == nullptr);
  EXPECT_FALSE(jwks->isExpired());

  // cache duration is 1 second, sleep two seconds to expire it
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_TRUE(jwks->isExpired());
}

// Test a good local jwks
TEST_F(JwksCacheTest, TestGoodInlineJwks) {
  auto rule0 = config_.mutable_rules(0);
  rule0->clear_remote_jwks();
  auto local_jwks = rule0->mutable_local_jwks();
  local_jwks->set_inline_string(PublicKey);

  cache_ = JwksCache::create(config_);

  auto jwks = cache_->findByIssuer("issuer1");
  EXPECT_FALSE(jwks->getJwksObj() == nullptr);
  EXPECT_FALSE(jwks->isExpired());
}

// Test a bad local jwks
TEST_F(JwksCacheTest, TestBadInlineJwks) {
  auto rule0 = config_.mutable_rules(0);
  rule0->clear_remote_jwks();
  auto local_jwks = rule0->mutable_local_jwks();
  local_jwks->set_inline_string("BAD-JWKS");

  cache_ = JwksCache::create(config_);

  auto jwks = cache_->findByIssuer("issuer1");
  EXPECT_TRUE(jwks->getJwksObj() == nullptr);
}

// Test audiences with different formats
TEST_F(JwksCacheTest, TestAudiences) {
  auto jwks = cache_->findByIssuer("issuer1");

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

  // incomding has https://, config is http://
  // incoming has tailing slash, config has not tailing slash
  EXPECT_TRUE(jwks->areAudiencesAllowed({"https://example_service1/"}));

  // incoming without tailing slash, config has tailing slash
  // incomding has http://, config is https://
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
