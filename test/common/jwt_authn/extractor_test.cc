#include "common/jwt_authn/extractor.h"

#include "common/protobuf/utility.h"
#include "test/test_common/utility.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::_;

namespace Envoy {
namespace JwtAuthn {
namespace {

const char ExampleConfig[] = R"(
- rules:
   issuer: issuer1
- rules:
   issuer: issuer2
   from_headers:
     - name: token-header
- rules:
   issuer: issuer3
   from_params:
     - token_param
- rules:
   issuer: issuer4
   from_headers:
     - name: token-header
   from_params:
     - token_param
- rules:
   issuer: issuer5
   from_headers:
     - name: prefix-header
       value_prefix: prefix
)";

} //  namespace

class ExtractorTest : public ::testing::Test {
public:
  void SetUp() {
    MessageUtil::loadFromYaml(ExampleConfig, config_);
    extractor_.reset(new Extractor(config_));
  }

  JwtAuthentication config_;
  std::unique_ptr<Extractor> extractor_;
};

TEST_F(ExtractorTest, TestNoToken) {
  auto headers = TestHeaderMapImpl{};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 0);
}

TEST_F(ExtractorTest, TestWrongHeaderToken) {
  auto headers = TestHeaderMapImpl{{"wrong-token-header", "jwt_token"}};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 0);
}

TEST_F(ExtractorTest, TestWrongParamToken) {
  auto headers = TestHeaderMapImpl{{":path", "/path?wrong_token=jwt_token"}};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 0);
}

TEST_F(ExtractorTest, TestDefaultHeaderLocation) {
  auto headers = TestHeaderMapImpl{{"Authorization", "Bearer jwt_token"}};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0]->token(), "jwt_token");

  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer1"));

  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer4"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));

  // Test token remove
  tokens[0]->Remove(&headers);
  EXPECT_FALSE(headers.Authorization());
}

TEST_F(ExtractorTest, TestDefaultParamLocation) {
  auto headers = TestHeaderMapImpl{{":path", "/path?access_token=jwt_token"}};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0]->token(), "jwt_token");

  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer1"));

  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer4"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));
}

TEST_F(ExtractorTest, TestCustomHeaderToken) {
  auto headers = TestHeaderMapImpl{{"token-header", "jwt_token"}};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 1);

  EXPECT_EQ(tokens[0]->token(), "jwt_token");

  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer1"));
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer4"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));

  // Test token remove
  tokens[0]->Remove(&headers);
  EXPECT_FALSE(headers.get(LowerCaseString("token-header")));
}

TEST_F(ExtractorTest, TestCustomParamToken) {
  auto headers = TestHeaderMapImpl{{":path", "/path?token_param=jwt_token"}};
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 1);

  EXPECT_EQ(tokens[0]->token(), "jwt_token");

  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer1"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer2"));
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer3"));
  EXPECT_TRUE(tokens[0]->isIssuerSpecified("issuer4"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("issuer5"));
  EXPECT_FALSE(tokens[0]->isIssuerSpecified("unknown_issuer"));
}

TEST_F(ExtractorTest, TestMultipleTokens) {
  auto headers = TestHeaderMapImpl{{":path", "/path?token_param=token3&access_token=token4"},
                                   {"token-header", "token2"},
                                   {"authorization", "Bearer token1"},
                                   {"prefix-header", "prefixtoken5"},
  };
  std::vector<JwtLocationPtr> tokens;
  extractor_->extract(headers, &tokens);
  EXPECT_EQ(tokens.size(), 1);

  // Header token first.
  EXPECT_EQ(tokens[0]->token(), "header_token");
}

} // namespace JwtAuth
} // namespace Http
} // namespace Envoy
