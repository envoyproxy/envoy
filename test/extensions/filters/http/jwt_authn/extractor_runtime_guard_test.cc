#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/jwt_authn/extractor.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using Envoy::Http::TestRequestHeaderMapImpl;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

const char ExampleConfig[] = R"(
providers:
  provider1:
    issuer: issuer1
)";

class ExtractorRuntimeGuardTest : public testing::Test {
public:
  void SetUp() override { setUp(ExampleConfig); }

  void setUp(const std::string& config_str) {
    TestUtility::loadFromYaml(config_str, config_);
    JwtProviderList providers;
    for (const auto& it : config_.providers()) {
      providers.emplace_back(&it.second);
    }
    extractor_ = Extractor::create(providers);
  }

  JwtAuthentication config_;
  ExtractorConstPtr extractor_;
};

// Test extracting JWT as Bearer token from the default header location: "Authorization" -
// using an actual (correctly-formatted) JWT but token is invalid, like: GoodToken +
// chars_after_space expected to get all token include characters after the space:
TEST_F(ExtractorRuntimeGuardTest,
       TestDefaultHeaderLocationWithValidJWTEndedWithSpaceAndMoreCharachters) {
  std::string chars_after_space = "jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj";
  std::string concatenated = std::string(GoodToken) + " ." + chars_after_space;
  const char* valid_token_with_space_and_chars = concatenated.c_str();
  auto headers = TestRequestHeaderMapImpl{
      {absl::StrCat("Authorization"), absl::StrCat("Bearer ", valid_token_with_space_and_chars)}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Only the issue1 is using default header location.
  EXPECT_EQ(tokens[0]->token(), std::string(GoodToken));
  EXPECT_TRUE(tokens[0]->isIssuerAllowed("issuer1"));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
