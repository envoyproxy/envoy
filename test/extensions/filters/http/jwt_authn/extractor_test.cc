#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/extractor.h"

#include "test/test_common/utility.h"

using ::Envoy::Http::TestHeaderMapImpl;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

const char ExampleConfig[] = R"(
rules:
  - issuer: issuer1
  - issuer: issuer2
    from_headers:
      - name: token-header
  - issuer: issuer3
    from_params:
      - token_param
  - issuer: issuer4
    from_headers:
      - name: token-header
    from_params:
      - token_param
  - issuer: issuer5
    from_headers:
      - name: prefix-header
        value_prefix: AAA
  - issuer: issuer6
    from_headers:
      - name: prefix-header
        value_prefix: AAABBB
)";

class ExtractorTest : public ::testing::Test {
public:
  void SetUp() {
    MessageUtil::loadFromYaml(ExampleConfig, config_);
    extractor_ = Extractor::create(config_);
  }

  JwtAuthentication config_;
  ExtractorConstPtr extractor_;
};

// Test not token in the request headers
TEST_F(ExtractorTest, TestNoToken) {
  auto headers = TestHeaderMapImpl{};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test the token in the wrong header.
TEST_F(ExtractorTest, TestWrongHeaderToken) {
  auto headers = TestHeaderMapImpl{{"wrong-token-header", "jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test the token in the wrong query parameter.
TEST_F(ExtractorTest, TestWrongParamToken) {
  auto headers = TestHeaderMapImpl{{":path", "/path?wrong_token=jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test extracting token from the default header location: "Authorization"
TEST_F(ExtractorTest, TestDefaultHeaderLocation) {
  auto headers = TestHeaderMapImpl{{"Authorization", "Bearer jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Only the issue1 is using default header location.
  EXPECT_EQ(tokens[0]->token(), "jwt_token");
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer1"));

  // Other issuers are using custom locations
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer4"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));

  // Test token remove
  tokens[0]->removeJwt(headers);
  EXPECT_FALSE(headers.Authorization());
}

// Test extracting token from the default query parameter: "access_token"
TEST_F(ExtractorTest, TestDefaultParamLocation) {
  auto headers = TestHeaderMapImpl{{":path", "/path?access_token=jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Only the issue1 is using default header location.
  EXPECT_EQ(tokens[0]->token(), "jwt_token");
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer1"));

  // Other issuers are using custom locations
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer4"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));

  tokens[0]->removeJwt(headers);
}

// Test extracting token from the custom header: "token-header"
TEST_F(ExtractorTest, TestCustomHeaderToken) {
  auto headers = TestHeaderMapImpl{{"token-header", "jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Only issuer2 and issuer4 are using "token-header" location
  EXPECT_EQ(tokens[0]->token(), "jwt_token");
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer4"));

  // Other issuers are not allowed from "token-header"
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer1"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));

  // Test token remove
  tokens[0]->removeJwt(headers);
  EXPECT_FALSE(headers.get(Http::LowerCaseString("token-header")));
}

// Test extracting token from the custom header: "prefix-header"
// value prefix doesn't match. It has to be eitehr "AAA" or "AAABBB".
TEST_F(ExtractorTest, TestPrefixHeaderNotMatch) {
  auto headers = TestHeaderMapImpl{{"prefix-header", "jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test extracting token from the custom header: "prefix-header"
// The value matches both prefix values: "AAA" or "AAABBB".
TEST_F(ExtractorTest, TestPrefixHeaderMatch) {
  auto headers = TestHeaderMapImpl{{"prefix-header", "AAABBBjwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 2);

  // Match issuer 5 with map key as: prefix-header + AAA
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_EQ(tokens[0]->token(), "BBBjwt_token");

  // Match issuer 6 with map key as: prefix-header + AAABBB which is after AAA
  EXPECT_TRUE(tokens[1]->isIssuerSpecified("issuer6"));
  EXPECT_EQ(tokens[1]->token(), "jwt_token");

  // Test token remove
  tokens[0]->removeJwt(headers);
  EXPECT_FALSE(headers.get(Http::LowerCaseString("prefix-header")));
}

// Test extracting token from the custom query parameter: "token_param"
TEST_F(ExtractorTest, TestCustomParamToken) {
  auto headers = TestHeaderMapImpl{{":path", "/path?token_param=jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Both issuer3 and issuer4 have specified this custom query location.
  EXPECT_EQ(tokens[0]->token(), "jwt_token");
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer4"));

  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer1"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));

  tokens[0]->removeJwt(headers);
}

// Test extracting multiple tokens.
TEST_F(ExtractorTest, TestMultipleTokens) {
  auto headers = TestHeaderMapImpl{
      {":path", "/path?token_param=token3&access_token=token4"},
      {"token-header", "token2"},
      {"authorization", "Bearer token1"},
      {"prefix-header", "AAAtoken5"},
  };
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 5);

  EXPECT_EQ(tokens[0]->token(), "token1"); // from authorization
  EXPECT_EQ(tokens[1]->token(), "token5"); // from prefix-header
  EXPECT_EQ(tokens[2]->token(), "token2"); // from token-header
  EXPECT_EQ(tokens[3]->token(), "token4"); // from access_token param
  EXPECT_EQ(tokens[4]->token(), "token3"); // from token_param param
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
