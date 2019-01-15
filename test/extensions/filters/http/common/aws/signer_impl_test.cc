#include "common/buffer/buffer_impl.h"
#include "common/http/message_impl.h"

#include "extensions/filters/http/common/aws/signer_impl.h"
#include "extensions/filters/http/common/aws/utility.h"

#include "test/extensions/filters/http/common/aws/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

class SignerImplTest : public testing::Test {
public:
  SignerImplTest()
      : credentials_provider_(new NiceMock<MockCredentialsProvider>()),
        message_(new Http::RequestMessageImpl()),
        signer_("service", "region", CredentialsProviderSharedPtr{credentials_provider_},
                time_system_),
        credentials_("akid", "secret"), token_credentials_("akid", "secret", "token") {
    // 20180102T030405Z
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void addMethod(const std::string& method) { message_->headers().insertMethod().value(method); }

  void addPath(const std::string& path) { message_->headers().insertPath().value(path); }

  void addHeader(const std::string& key, const std::string& value) {
    message_->headers().addCopy(Http::LowerCaseString(key), value);
  }

  void setBody(const std::string& body) {
    message_->body() = std::make_unique<Buffer::OwnedImpl>(body);
  }

  std::string canonicalRequest() {
    const auto canonical_headers = Utility::canonicalizeHeaders(message_->headers());
    const auto signing_headers = signer_.createSigningHeaders(canonical_headers);
    const auto content_hash = signer_.createContentHash(*message_);
    return signer_.createCanonicalRequest(*message_, canonical_headers, signing_headers,
                                          content_hash);
  }

  NiceMock<MockCredentialsProvider>* credentials_provider_;
  Event::SimulatedTimeSystem time_system_;
  Http::MessagePtr message_;
  SignerImpl signer_;
  Credentials credentials_;
  Credentials token_credentials_;
  absl::optional<std::string> region_;
};

// No authorization header should be present when the credentials are empty
TEST_F(SignerImplTest, AnonymousCredentials) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(Credentials()));
  signer_.sign(*message_);
  EXPECT_EQ(nullptr, message_->headers().Authorization());
}

// Verify we sign the security token header if the token is present in the credentials
TEST_F(SignerImplTest, SignSecurityTokenHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(token_credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_);
  EXPECT_STREQ("token",
               message_->headers().get(SignatureHeaders::get().SecurityToken)->value().c_str());
  EXPECT_STREQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
               "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-security-token, "
               "Signature=1d42526aabf7d8b6d7d33d9db43b03537300cc7e6bb2817e349749e0a08f5b5e",
               message_->headers().Authorization()->value().c_str());
}

// Verify we sign the date header
TEST_F(SignerImplTest, SignDateHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_);
  EXPECT_STREQ("20180102T030400Z",
               message_->headers().get(SignatureHeaders::get().Date)->value().c_str());
  EXPECT_STREQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
               "SignedHeaders=x-amz-content-sha256;x-amz-date, "
               "Signature=4ee6aa9355259c18133f150b139ea9aeb7969c9408ad361b2151f50a516afe42",
               message_->headers().Authorization()->value().c_str());
}

// Verify we sign the content header as the hashed empty string if the body is empty
TEST_F(SignerImplTest, SignEmptyContentHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/empty?content=none");
  signer_.sign(*message_);
  EXPECT_EQ(
      SignatureConstants::get().HashedEmptyString,
      message_->headers().get(SignatureHeaders::get().ContentSha256)->value().getStringView());
  EXPECT_STREQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
               "SignedHeaders=x-amz-content-sha256;x-amz-date, "
               "Signature=999e211bc7134cc685f830a332cf4d871b6d8bb8ced9367c1a0b59b95a03ee7d",
               message_->headers().Authorization()->value().c_str());
}

// Verify we sign the content header correctly when we have a body
TEST_F(SignerImplTest, SignContentHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("POST");
  addPath("/");
  setBody("test1234");
  signer_.sign(*message_);
  EXPECT_STREQ("937e8d5fbb48bd4949536cd65b8d35c426b80d2f830c5c308e2cdec422ae2244",
               message_->headers().get(SignatureHeaders::get().ContentSha256)->value().c_str());
  EXPECT_STREQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
               "SignedHeaders=x-amz-content-sha256;x-amz-date, "
               "Signature=4eab89c36f45f2032d6010ba1adab93f8510ddd6afe540821f3a05bb0253e27b",
               message_->headers().Authorization()->value().c_str());
}

// HTTP :method header is required
TEST_F(SignerImplTest, MissingMethodException) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  EXPECT_THROW_WITH_MESSAGE(signer_.sign(*message_), EnvoyException,
                            "Message is missing :method header");
  EXPECT_EQ(nullptr, message_->headers().Authorization());
}

// HTTP :path header is required
TEST_F(SignerImplTest, MissingPathException) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  EXPECT_THROW_WITH_MESSAGE(signer_.sign(*message_), EnvoyException,
                            "Message is missing :path header");
  EXPECT_EQ(nullptr, message_->headers().Authorization());
}

// Verify that a correct number of blank lines exist in a minimalist canonical request
TEST_F(SignerImplTest, EmptyCanonicalRequest) {
  addMethod("POST");
  addPath("/hello");
  EXPECT_EQ(R"(POST
/hello



e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)",
            canonicalRequest());
}

// Verify that we add the query string to the canonical request
TEST_F(SignerImplTest, QueryStringInCanonicalRequest) {
  addMethod("POST");
  addPath("/path/foo?bar=baz");
  EXPECT_EQ(R"(POST
/path/foo
bar=baz


e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)",
            canonicalRequest());
}

// Verify that we use an empty path if we only have a query string
TEST_F(SignerImplTest, OnlyQueryStringInCanonicalRequest) {
  addMethod("POST");
  addPath("?bar=baz");
  EXPECT_EQ(R"(POST
/
bar=baz


e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)",
            canonicalRequest());
}

// Verify that we insert an extra blank line after the canonical headers
TEST_F(SignerImplTest, ExtraNewlineAfterCanonicalizedHeaders) {
  addMethod("POST");
  addPath("/?query");
  addHeader("header1", "header1 value");
  addHeader("header2", "header2 value");
  addHeader("header3", "header3 value");
  EXPECT_EQ(R"(POST
/
query
header1:header1 value
header2:header2 value
header3:header3 value

header1;header2;header3
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)",
            canonicalRequest());
}

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy