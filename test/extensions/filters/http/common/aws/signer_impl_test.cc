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
namespace {

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

// Verify we sign the date header
TEST_F(SignerImplTest, SignDateHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_);
  EXPECT_EQ(nullptr, message_->headers().get(SignatureHeaders::get().ContentSha256));
  EXPECT_EQ("20180102T030400Z",
            message_->headers().get(SignatureHeaders::get().Date)->value().getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-date, "
            "Signature=1310784f67248cab70d98b9404d601f30d8fe20bd1820560cce224f4131dc1cc",
            message_->headers().Authorization()->value().getStringView());
}

// Verify we sign the security token header if the token is present in the credentials
TEST_F(SignerImplTest, SignSecurityTokenHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(token_credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_);
  EXPECT_EQ(
      "token",
      message_->headers().get(SignatureHeaders::get().SecurityToken)->value().getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-date;x-amz-security-token, "
            "Signature=ff1d9fa7e54a72677b5336df047bb1f1493f86b92099973bf62da3af852d1679",
            message_->headers().Authorization()->value().getStringView());
}

// Verify we sign the content header as the hashed empty string if the body is empty
TEST_F(SignerImplTest, SignEmptyContentHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_, true);
  EXPECT_EQ(
      SignatureConstants::get().HashedEmptyString,
      message_->headers().get(SignatureHeaders::get().ContentSha256)->value().getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date, "
            "Signature=4ee6aa9355259c18133f150b139ea9aeb7969c9408ad361b2151f50a516afe42",
            message_->headers().Authorization()->value().getStringView());
}

// Verify we sign the content header correctly when we have a body
TEST_F(SignerImplTest, SignContentHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("POST");
  addPath("/");
  setBody("test1234");
  signer_.sign(*message_, true);
  EXPECT_EQ(
      "937e8d5fbb48bd4949536cd65b8d35c426b80d2f830c5c308e2cdec422ae2244",
      message_->headers().get(SignatureHeaders::get().ContentSha256)->value().getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date, "
            "Signature=4eab89c36f45f2032d6010ba1adab93f8510ddd6afe540821f3a05bb0253e27b",
            message_->headers().Authorization()->value().getStringView());
}

// Verify we sign some extra headers
TEST_F(SignerImplTest, SignExtraHeaders) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  addHeader("a", "a_value");
  addHeader("b", "b_value");
  addHeader("c", "c_value");
  signer_.sign(*message_);
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=a;b;c;x-amz-date, "
            "Signature=d5e025e1cf0d5af0d83110bc2ef1cafd2d9dca1dea9d7767f58308da64aa6558",
            message_->headers().Authorization()->value().getStringView());
}

// Verify signing a host header
TEST_F(SignerImplTest, SignHostHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  addHeader("host", "www.example.com");
  signer_.sign(*message_);
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=host;x-amz-date, "
            "Signature=60216ee44dd651322ea10cc6747308dd30e582aaa773f6c1b1354e486385c021",
            message_->headers().Authorization()->value().getStringView());
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
