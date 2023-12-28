#include "source/common/buffer/buffer_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/sigv4a_key_derivation.h"
#include "source/extensions/common/aws/sigv4a_signer_impl.h"
#include "source/extensions/common/aws/utility.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

class SigV4ASignerImplTest : public testing::Test {
public:
  SigV4ASignerImplTest()
      : credentials_provider_(new NiceMock<MockCredentialsProvider>()),
        message_(new Http::RequestMessageImpl()),
        signer_("service", "region", CredentialsProviderSharedPtr{credentials_provider_},
                time_system_, Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}),
        credentials_("akid", "secret"), token_credentials_("akid", "secret", "token") {
    // 20180102T030405Z
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
  }

  void addMethod(const std::string& method) { message_->headers().setMethod(method); }

  void addPath(const std::string& path) { message_->headers().setPath(path); }

  void addHeader(const std::string& key, const std::string& value) {
    message_->headers().addCopy(Http::LowerCaseString(key), value);
  }

  void setBody(const std::string& body) { message_->body().add(body); }

  NiceMock<MockCredentialsProvider>* credentials_provider_;
  Event::SimulatedTimeSystem time_system_;
  Http::RequestMessagePtr message_;
  SigV4ASignerImpl signer_;
  Credentials credentials_;
  Credentials token_credentials_;
  absl::optional<std::string> region_;
};

// No authorization header should be present when the credentials are empty
TEST_F(SigV4ASignerImplTest, AnonymousCredentials) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(Credentials()));
  signer_.sign(*message_);
  EXPECT_TRUE(message_->headers().get(Http::CustomHeaders::get().Authorization).empty());
}

// HTTP :method header is required
TEST_F(SigV4ASignerImplTest, MissingMethodException) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  EXPECT_THROW_WITH_MESSAGE(signer_.sign(*message_), EnvoyException,
                            "Message is missing :method header");
  EXPECT_TRUE(message_->headers().get(Http::CustomHeaders::get().Authorization).empty());
}

// HTTP :path header is required
TEST_F(SigV4ASignerImplTest, MissingPathException) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  EXPECT_THROW_WITH_MESSAGE(signer_.sign(*message_), EnvoyException,
                            "Message is missing :path header");
  EXPECT_TRUE(message_->headers().get(Http::CustomHeaders::get().Authorization).empty());
}

// Verify we sign the date header
TEST_F(SigV4ASignerImplTest, SignDateHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_);
  EXPECT_FALSE(message_->headers().get(SigV4ASignatureHeaders::get().ContentSha256).empty());
  EXPECT_EQ(
      "20180102T030400Z",
      message_->headers().get(SigV4ASignatureHeaders::get().Date)[0]->value().getStringView());
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
}

// Verify we sign the security token header if the token is present in the credentials
TEST_F(SigV4ASignerImplTest, SignSecurityTokenHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(token_credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_);
  EXPECT_EQ("token", message_->headers()
                         .get(SigV4ASignatureHeaders::get().SecurityToken)[0]
                         ->value()
                         .getStringView());
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith(
          "AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
          "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-region-set;x-amz-security-token, "
          "Signature="));
}

// Verify we sign the content header as the hashed empty string if the body is empty
TEST_F(SigV4ASignerImplTest, SignEmptyContentHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  signer_.sign(*message_, true);
  EXPECT_EQ(SigV4ASignatureConstants::get().HashedEmptyString,
            message_->headers()
                .get(SigV4ASignatureHeaders::get().ContentSha256)[0]
                ->value()
                .getStringView());
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
}

// Verify we sign the content header correctly when we have a body
TEST_F(SigV4ASignerImplTest, SignContentHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("POST");
  addPath("/");
  setBody("test1234");
  signer_.sign(*message_, true);
  EXPECT_EQ("937e8d5fbb48bd4949536cd65b8d35c426b80d2f830c5c308e2cdec422ae2244",
            message_->headers()
                .get(SigV4ASignatureHeaders::get().ContentSha256)[0]
                ->value()
                .getStringView());
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
}

// Verify we sign the content header correctly when we have a body with region override
TEST_F(SigV4ASignerImplTest, SignContentHeaderOverrideRegion) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("POST");
  addPath("/");
  setBody("test1234");
  signer_.sign(*message_, true, "region1");
  EXPECT_EQ("937e8d5fbb48bd4949536cd65b8d35c426b80d2f830c5c308e2cdec422ae2244",
            message_->headers()
                .get(SigV4ASignatureHeaders::get().ContentSha256)[0]
                ->value()
                .getStringView());
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
}

// Verify we sign some extra headers
TEST_F(SigV4ASignerImplTest, SignExtraHeaders) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  addHeader("a", "a_value");
  addHeader("b", "b_value");
  addHeader("c", "c_value");
  signer_.sign(*message_);
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=a;b;c;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
}

// Verify signing a host header
TEST_F(SigV4ASignerImplTest, SignHostHeader) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  addHeader("host", "www.example.com");
  signer_.sign(*message_);
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
}

TEST_F(SigV4ASignerImplTest, SignAndVerify) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  EC_KEY* ec_key = SigV4AKeyDerivation::derivePrivateKey(
      absl::string_view(credentials_.accessKeyId()->data(), credentials_.accessKeyId()->size()),
      absl::string_view(credentials_.secretAccessKey()->data(),
                        credentials_.secretAccessKey()->size()));
  SigV4AKeyDerivation::derivePublicKey(ec_key);

  addMethod("GET");
  addPath("/");
  addHeader("host", "www.example.com");

  // Sign the message using our signing algorithm
  signer_.sign(*message_, false, "ap-southeast-2");

  // Now manually sign the same string_to_sign
  std::string canonical_request = R"EOF(GET
/

host:www.example.com
x-amz-content-sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
x-amz-date:20180102T030400Z
x-amz-region-set:ap-southeast-2

host;x-amz-content-sha256;x-amz-date;x-amz-region-set
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)EOF";

  std::string short_date = "20180102";
  std::string credential_scope = fmt::format(fmt::runtime("{}/service/aws4_request"), short_date);
  std::string long_date = "20180102T030400Z";
  std::string string_to_sign =
      fmt::format(fmt::runtime(SigV4ASignatureConstants::get().SigV4AStringToSignFormat), long_date,
                  credential_scope,
                  Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));

  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));
  // Extract the signature that is generated
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
  std::vector<std::string> v = absl::StrSplit(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Signature=");

  std::vector<uint8_t> signature = Hex::decode(v[1]);
  // Check that the signature generated by our algorithm can be verified by the matching public key
  EXPECT_EQ(1,
            ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
  EC_KEY_free(ec_key);
}

TEST_F(SigV4ASignerImplTest, SignAndVerifyMultiRegion) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  EC_KEY* ec_key = SigV4AKeyDerivation::derivePrivateKey(
      absl::string_view(credentials_.accessKeyId()->data(), credentials_.accessKeyId()->size()),
      absl::string_view(credentials_.secretAccessKey()->data(),
                        credentials_.secretAccessKey()->size()));
  SigV4AKeyDerivation::derivePublicKey(ec_key);

  addMethod("GET");
  addPath("/");
  addHeader("host", "www.example.com");

  // Sign the message using our signing algorithm
  signer_.sign(*message_, false, "ap-southeast-2,us-east-1");

  // Now manually sign the same string_to_sign
  std::string canonical_request = R"EOF(GET
/

host:www.example.com
x-amz-content-sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
x-amz-date:20180102T030400Z
x-amz-region-set:ap-southeast-2,us-east-1

host;x-amz-content-sha256;x-amz-date;x-amz-region-set
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)EOF";

  std::string short_date = "20180102";
  std::string credential_scope = fmt::format(fmt::runtime("{}/service/aws4_request"), short_date);
  std::string long_date = "20180102T030400Z";
  std::string string_to_sign =
      fmt::format(fmt::runtime(SigV4ASignatureConstants::get().SigV4AStringToSignFormat), long_date,
                  credential_scope,
                  Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));
  // Extract the signature that is generated
  EXPECT_THAT(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
  std::vector<std::string> v = absl::StrSplit(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Signature=");
  std::vector<uint8_t> signature = Hex::decode(v[1]);
  // Check that the signature generated by our algorithm can be verified by the matching public key
  EXPECT_EQ(1,
            ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
  EC_KEY_free(ec_key);
}

TEST_F(SigV4ASignerImplTest, SignAndVerifyUnsignedPayload) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  EC_KEY* ec_key = SigV4AKeyDerivation::derivePrivateKey(
      absl::string_view(credentials_.accessKeyId()->data(), credentials_.accessKeyId()->size()),
      absl::string_view(credentials_.secretAccessKey()->data(),
                        credentials_.secretAccessKey()->size()));
  SigV4AKeyDerivation::derivePublicKey(ec_key);
  Http::TestRequestHeaderMapImpl headers{};

  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");

  // Sign the message using our signing algorithm
  signer_.signUnsignedPayload(headers, "ap-southeast-2");

  // Now manually sign the same string_to_sign
  std::string canonical_request = R"EOF(GET
/

host:www.example.com
x-amz-content-sha256:UNSIGNED-PAYLOAD
x-amz-date:20180102T030400Z
x-amz-region-set:ap-southeast-2

host;x-amz-content-sha256;x-amz-date;x-amz-region-set
UNSIGNED-PAYLOAD)EOF";

  std::string short_date = "20180102";
  std::string credential_scope = fmt::format(fmt::runtime("{}/service/aws4_request"), short_date);
  std::string long_date = "20180102T030400Z";
  std::string string_to_sign =
      fmt::format(fmt::runtime(SigV4ASignatureConstants::get().SigV4AStringToSignFormat), long_date,
                  credential_scope,
                  Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));
  // Extract the signature that is generated
  EXPECT_THAT(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
  std::vector<std::string> v = absl::StrSplit(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Signature=");
  std::vector<uint8_t> signature = Hex::decode(v[1]);
  // Check that the signature generated by our algorithm can be verified by the matching public key
  EXPECT_EQ(1,
            ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
  EC_KEY_free(ec_key);
}

TEST_F(SigV4ASignerImplTest, SignAndVerifyUnsignedPayloadMultiRegion) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  EC_KEY* ec_key = SigV4AKeyDerivation::derivePrivateKey(
      absl::string_view(credentials_.accessKeyId()->data(), credentials_.accessKeyId()->size()),
      absl::string_view(credentials_.secretAccessKey()->data(),
                        credentials_.secretAccessKey()->size()));
  SigV4AKeyDerivation::derivePublicKey(ec_key);

  Http::TestRequestHeaderMapImpl headers{};

  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");

  // Sign the message using our signing algorithm
  signer_.signUnsignedPayload(headers, "ap-southeast-2,us-east-*");

  // Now manually sign the same string_to_sign
  std::string canonical_request = R"EOF(GET
/

host:www.example.com
x-amz-content-sha256:UNSIGNED-PAYLOAD
x-amz-date:20180102T030400Z
x-amz-region-set:ap-southeast-2,us-east-*

host;x-amz-content-sha256;x-amz-date;x-amz-region-set
UNSIGNED-PAYLOAD)EOF";

  std::string short_date = "20180102";
  std::string credential_scope = fmt::format(fmt::runtime("{}/service/aws4_request"), short_date);
  std::string long_date = "20180102T030400Z";
  std::string string_to_sign =
      fmt::format(fmt::runtime(SigV4ASignatureConstants::get().SigV4AStringToSignFormat), long_date,
                  credential_scope,
                  Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));
  // Extract the signature that is generated
  EXPECT_THAT(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
  std::vector<std::string> v = absl::StrSplit(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Signature=");
  std::vector<uint8_t> signature = Hex::decode(v[1]);
  // Check that the signature generated by our algorithm can be verified by the matching public key
  EXPECT_EQ(1,
            ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
  EC_KEY_free(ec_key);
}

TEST_F(SigV4ASignerImplTest, SignAndVerifyEmptyPayload) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  EC_KEY* ec_key = SigV4AKeyDerivation::derivePrivateKey(
      absl::string_view(credentials_.accessKeyId()->data(), credentials_.accessKeyId()->size()),
      absl::string_view(credentials_.secretAccessKey()->data(),
                        credentials_.secretAccessKey()->size()));
  SigV4AKeyDerivation::derivePublicKey(ec_key);
  Http::TestRequestHeaderMapImpl headers{};

  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");

  // Sign the message using our signing algorithm
  signer_.signEmptyPayload(headers, "ap-southeast-2");

  // Now manually sign the same string_to_sign
  std::string canonical_request = R"EOF(GET
/

host:www.example.com
x-amz-content-sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
x-amz-date:20180102T030400Z
x-amz-region-set:ap-southeast-2

host;x-amz-content-sha256;x-amz-date;x-amz-region-set
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)EOF";

  std::string short_date = "20180102";
  std::string credential_scope = fmt::format(fmt::runtime("{}/service/aws4_request"), short_date);
  std::string long_date = "20180102T030400Z";
  std::string string_to_sign =
      fmt::format(fmt::runtime(SigV4ASignatureConstants::get().SigV4AStringToSignFormat), long_date,
                  credential_scope,
                  Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));
  // Extract the signature that is generated
  EXPECT_THAT(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
  std::vector<std::string> v = absl::StrSplit(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Signature=");
  std::vector<uint8_t> signature = Hex::decode(v[1]);
  // Check that the signature generated by our algorithm can be verified by the matching public key
  EXPECT_EQ(1,
            ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
  EC_KEY_free(ec_key);
}

TEST_F(SigV4ASignerImplTest, SignAndVerifyEmptyPayloadMultiRegion) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  EC_KEY* ec_key = SigV4AKeyDerivation::derivePrivateKey(
      absl::string_view(credentials_.accessKeyId()->data(), credentials_.accessKeyId()->size()),
      absl::string_view(credentials_.secretAccessKey()->data(),
                        credentials_.secretAccessKey()->size()));
  SigV4AKeyDerivation::derivePublicKey(ec_key);

  Http::TestRequestHeaderMapImpl headers{};

  headers.setMethod("GET");
  headers.setPath("/");
  headers.addCopy(Http::LowerCaseString("host"), "www.example.com");

  // Sign the message using our signing algorithm
  signer_.signEmptyPayload(headers, "ap-southeast-2,us-east-*");

  // Now manually sign the same string_to_sign
  std::string canonical_request = R"EOF(GET
/

host:www.example.com
x-amz-content-sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
x-amz-date:20180102T030400Z
x-amz-region-set:ap-southeast-2,us-east-*

host;x-amz-content-sha256;x-amz-date;x-amz-region-set
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855)EOF";

  std::string short_date = "20180102";
  std::string credential_scope = fmt::format(fmt::runtime("{}/service/aws4_request"), short_date);
  std::string long_date = "20180102T030400Z";
  std::string string_to_sign =
      fmt::format(fmt::runtime(SigV4ASignatureConstants::get().SigV4AStringToSignFormat), long_date,
                  credential_scope,
                  Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));
  // Extract the signature that is generated
  EXPECT_THAT(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      testing::StartsWith("AWS4-ECDSA-P256-SHA256 Credential=akid/20180102/service/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-region-set, "
                          "Signature="));
  std::vector<std::string> v = absl::StrSplit(
      headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "Signature=");
  std::vector<uint8_t> signature = Hex::decode(v[1]);
  // Check that the signature generated by our algorithm can be verified by the matching public key
  EXPECT_EQ(1,
            ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
  EC_KEY_free(ec_key);
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
