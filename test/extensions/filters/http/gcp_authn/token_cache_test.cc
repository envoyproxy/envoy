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

// Good token
// {"iss":"https://example.com","sub":"test@example.com", "aud":"example_service",
// "exp":2001001001 - Sun May 29 2033 13:36:41 GMT-0400}
constexpr absl::string_view GoodTokenStr =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.cuui_Syud76B0tqvjESE8IZbX7vzG6xA-M"
    "Daof1qEFNIoCFT_YQPkseLSUSR2Od3TJcNKk-dKjvUEL1JW3kGnyC1dBx4f3-Xxro"
    "yL23UbR2eS8TuxO9ZcNCGkjfvH5O4mDb6cVkFHRDEolGhA7XwNiuVgkGJ5Wkrvshi"
    "h6nqKXcPNaRx9lOaRWg2PkE6ySNoyju7rNfunXYtVxPuUIkl0KMq3WXWRb_cb8a_Z"
    "EprqSZUzi_ZzzYzqBNVhIJujcNWij7JRra2sXXiSAfKjtxHQoxrX8n4V1ySWJ3_1T"
    "H_cJcdfS_RKP7YgXRWC0L16PNF5K7iqRqmjKALNe83ZFnFIw";
// The value of exp time field in the token above.
const time_t ExpTime = 2001001001;

// Expired token
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service",
// "exp":1205005587 - Sat Mar 08 2008 14:46:27 GMT-0500}
constexpr absl::string_view ExpiredToken =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MTIwNTAwNTU4NywiY"
    "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.izDa6aHNgbsbeRzucE0baXIP7SXOrgopYQ"
    "ALLFAsKq_N0GvOyqpAZA9nwCAhqCkeKWcL-9gbQe3XJa0KN3FPa2NbW4ChenIjmf2"
    "QYXOuOQaDu9QRTdHEY2Y4mRy6DiTZAsBHWGA71_cLX-rzTSO_8aC8eIqdHo898oJw"
    "3E8ISKdryYjayb9X3wtF6KLgNomoD9_nqtOkliuLElD8grO0qHKI1xQurGZNaoeyi"
    "V1AdwgX_5n3SmQTacVN0WcSgk6YJRZG6VE8PjxZP9bEameBmbSB0810giKRpdTU1-"
    "RJtjq6aCSTD4CYXtW38T5uko4V-S4zifK3BXeituUTebkgoA";

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
    token_cache_ = std::make_unique<TokenCacheImpl<JwtToken>>(config.cache_config(), time_system_);

    jwt_ = std::make_unique<::google::jwt_verify::Jwt>();
  }

  NiceMock<MockFactoryContext> context_;
  std::unique_ptr<TokenCacheImpl<JwtToken>> token_cache_;
  Event::SimulatedTimeSystem time_system_;
  std::unique_ptr<::google::jwt_verify::Jwt> jwt_ = nullptr;
};

TEST_F(TokenCacheTest, ValidToken) {
  EXPECT_EQ(token_cache_->capacity(), 100);
  std::string good_token = std::string(GoodTokenStr);
  ::google::jwt_verify::Status status = jwt_->parseFromString(good_token);
  EXPECT_TRUE(status == ::google::jwt_verify::Status::Ok);
  auto* old_jwt = jwt_.get();
  token_cache_->insert(good_token, std::move(jwt_));
  auto* found_jwt = token_cache_->lookUp(good_token);
  EXPECT_TRUE(found_jwt != nullptr);
  EXPECT_EQ(found_jwt, old_jwt);
}

TEST_F(TokenCacheTest, ExpiredToken) {
  std::string expired_token = std::string(ExpiredToken);
  ::google::jwt_verify::Status status = jwt_->parseFromString(expired_token);
  EXPECT_TRUE(status == ::google::jwt_verify::Status::Ok);
  token_cache_->insert(expired_token, std::move(jwt_));
  auto* found_jwt = token_cache_->lookUp(expired_token);
  EXPECT_TRUE(found_jwt == nullptr);
}

// Test the token with the clock skew.
TEST_F(TokenCacheTest, TokenWithClockSkew) {
  // Set the time to exp_time + 1s.
  // i.e., The expiration time in the token is `Sun May 29 2033 13:36:41 GMT-0400` and the time we
  // set to is `Sun May 29 2033 13:35:42 GMT-0400`.
  const time_t exp_time_with_skew = ExpTime - ::google::jwt_verify::kClockSkewInSecond;
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(exp_time_with_skew + 1));
  std::string token = std::string(GoodTokenStr);
  ::google::jwt_verify::Status status = jwt_->parseFromString(token);
  EXPECT_TRUE(status == ::google::jwt_verify::Status::Ok);
  token_cache_->insert(token, std::move(jwt_));
  auto* found_jwt = token_cache_->lookUp(token);
  EXPECT_TRUE(found_jwt == nullptr);

  std::unique_ptr<::google::jwt_verify::Jwt> jwt = std::make_unique<::google::jwt_verify::Jwt>();
  // Set the time to exp_time - 1s.
  time_system_.setSystemTime(std::chrono::system_clock::from_time_t(exp_time_with_skew));
  auto* old_jwt = jwt.get();
  token_cache_->insert(token, std::move(jwt));
  found_jwt = token_cache_->lookUp(token);
  EXPECT_TRUE(found_jwt != nullptr);
  EXPECT_EQ(found_jwt, old_jwt);
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
