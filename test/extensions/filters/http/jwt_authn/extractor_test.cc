#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/extractor.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::JwtProvider;
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
  provider2:
    issuer: issuer2
    from_headers:
      - name: token-header
  provider3:
    issuer: issuer3
    from_params:
      - token_param
  provider4:
    issuer: issuer4
    from_headers:
      - name: token-header
    from_params:
      - token_param
  provider5:
    issuer: issuer5
    from_headers:
      - name: prefix-header
        value_prefix: AAA
  provider6:
    issuer: issuer6
    from_headers:
      - name: prefix-header
        value_prefix: AAABBB
  provider7:
    issuer: issuer7
    from_headers:
      - name: prefix-header
        value_prefix: CCCDDD
  provider8:
    issuer: issuer8
    from_headers:
      - name: prefix-header
        value_prefix: '"CCCDDD"'
)";

class ExtractorTest : public testing::Test {
public:
  void SetUp() override {
    TestUtility::loadFromYaml(ExampleConfig, config_);
    JwtProviderList providers;
    for (const auto& it : config_.providers()) {
      providers.emplace_back(&it.second);
    }
    extractor_ = Extractor::create(providers);
  }

  JwtAuthentication config_;
  ExtractorConstPtr extractor_;
};

// Test not token in the request headers
TEST_F(ExtractorTest, TestNoToken) {
  auto headers = TestRequestHeaderMapImpl{};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test the token in the wrong header.
TEST_F(ExtractorTest, TestWrongHeaderToken) {
  auto headers = TestRequestHeaderMapImpl{{"wrong-token-header", "jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test the token in the wrong query parameter.
TEST_F(ExtractorTest, TestWrongParamToken) {
  auto headers = TestRequestHeaderMapImpl{{":path", "/path?wrong_token=jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test extracting token from the default header location: "Authorization"
TEST_F(ExtractorTest, TestDefaultHeaderLocation) {
  auto headers = TestRequestHeaderMapImpl{{"Authorization", "Bearer jwt_token"}};
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

// Test extracting JWT as Bearer token from the default header location: "Authorization" -
// using an actual (correctly-formatted) JWT:
TEST_F(ExtractorTest, TestDefaultHeaderLocationWithValidJWT) {
  auto headers =
      TestRequestHeaderMapImpl{{absl::StrCat("Authorization"), absl::StrCat("Bearer ", GoodToken)}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Only the issue1 is using default header location.
  EXPECT_EQ(tokens[0]->token(), GoodToken);
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer1"));
}

// Test extracting token from the default query parameter: "access_token"
TEST_F(ExtractorTest, TestDefaultParamLocation) {
  auto headers = TestRequestHeaderMapImpl{{":path", "/path?access_token=jwt_token"}};
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
  auto headers = TestRequestHeaderMapImpl{{"token-header", "jwt_token"}};
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
// value prefix doesn't match. It has to be either "AAA" or "AAABBB".
TEST_F(ExtractorTest, TestPrefixHeaderNotMatch) {
  auto headers = TestRequestHeaderMapImpl{{"prefix-header", "jwt_token"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 0);
}

// Test extracting token from the custom header: "prefix-header"
// The value matches both prefix values: "AAA" or "AAABBB".
TEST_F(ExtractorTest, TestPrefixHeaderMatch) {
  auto headers = TestRequestHeaderMapImpl{{"prefix-header", "AAABBBjwt_token"}};
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

// Test extracting token from the custom header: "prefix-header"
// The value is found after the "CCCDDD", then between the '=' and the ','.
TEST_F(ExtractorTest, TestPrefixHeaderFlexibleMatch1) {
  auto headers =
      TestRequestHeaderMapImpl{{"prefix-header", "preamble CCCDDD=jwt_token,extra=more"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Match issuer 7 with map key as: prefix-header + 'CCCDDD'
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer7"));
  EXPECT_EQ(tokens[0]->token(), "jwt_token");
}

TEST_F(ExtractorTest, TestPrefixHeaderFlexibleMatch2) {
  auto headers =
      TestRequestHeaderMapImpl{{"prefix-header", "CCCDDD=\"and0X3Rva2Vu\",comment=\"fish tag\""}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 1);

  // Match issuer 7 with map key as: prefix-header + AAA
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer7"));
  EXPECT_EQ(tokens[0]->token(), "and0X3Rva2Vu");
}

TEST_F(ExtractorTest, TestPrefixHeaderFlexibleMatch3) {
  auto headers = TestRequestHeaderMapImpl{
      {"prefix-header", "creds={\"authLevel\": \"20\", \"CCCDDD\": \"and0X3Rva2Vu\"}"}};
  auto tokens = extractor_->extract(headers);
  EXPECT_EQ(tokens.size(), 2);

  // Match issuer 8 with map key as: prefix-header + '"CCCDDD"'
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer8"));
  EXPECT_EQ(tokens[0]->token(), "and0X3Rva2Vu");

  // Match issuer 7 with map key as: prefix-header + 'CCCDDD'
  EXPECT_TRUE(tokens[1]->isIssuerSpecified("issuer7"));
  EXPECT_EQ(tokens[1]->token(), "and0X3Rva2Vu");
}

// Test extracting token from the custom query parameter: "token_param"
TEST_F(ExtractorTest, TestCustomParamToken) {
  auto headers = TestRequestHeaderMapImpl{{":path", "/path?token_param=jwt_token"}};
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
  auto headers = TestRequestHeaderMapImpl{
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

// Test selected extraction of multiple tokens.
TEST_F(ExtractorTest, TestExtractParam) {
  auto headers = TestRequestHeaderMapImpl{
      {":path", "/path?token_param=token3&access_token=token4"},
      {"token-header", "token2"},
      {"authorization", "Bearer token1"},
      {"prefix-header", "AAAtoken5"},
  };
  JwtProvider provider;
  provider.set_issuer("foo");
  auto extractor = Extractor::create(provider);
  auto tokens = extractor->extract(headers);
  EXPECT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0]->token(), "token1");
  EXPECT_EQ(tokens[1]->token(), "token4");
  auto header = provider.add_from_headers();
  header->set_name("prefix-header");
  header->set_value_prefix("AAA");
  provider.add_from_params("token_param");
  extractor = Extractor::create(provider);
  tokens = extractor->extract(headers);
  EXPECT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0]->token(), "token5");
  EXPECT_EQ(tokens[1]->token(), "token3");
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
