#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "source/extensions/common/aws/utility.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

class SigV4SignerImplTest : public testing::Test {
public:
  SigV4SignerImplTest()
      : credentials_provider_(new NiceMock<MockCredentialsProvider>()),
        message_(new Http::RequestMessageImpl()),
        signer_("service", "region", CredentialsProviderSharedPtr{credentials_provider_}, context_,
                Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}),
        credentials_("akid", "secret"), token_credentials_("akid", "secret", "token") {
    // 20180102T030405Z
    time_system_.setSystemTime(std::chrono::milliseconds(1514862245000));
    ON_CALL(context_, timeSystem()).WillByDefault(ReturnRef(time_system_));
  }

  void addMethod(const std::string& method) { message_->headers().setMethod(method); }

  void addPath(const std::string& path) { message_->headers().setPath(path); }

  void addHeader(const std::string& key, const std::string& value) {
    message_->headers().addCopy(Http::LowerCaseString(key), value);
  }

  void setBody(const std::string& body) { message_->body().add(body); }

  void expectSignHeaders(absl::string_view service_name, absl::string_view signature,
                         absl::string_view payload, bool use_unsigned_payload,
                         const absl::string_view override_region = "") {
    auto* credentials_provider = new NiceMock<MockCredentialsProvider>();
    EXPECT_CALL(*credentials_provider, getCredentials()).WillOnce(Return(credentials_));
    Http::TestRequestHeaderMapImpl headers{};
    absl::Status status;
    headers.setMethod("GET");
    headers.setPath("/");
    headers.addCopy(Http::LowerCaseString("host"), "www.example.com");

    SigV4SignerImpl signer(service_name, "region",
                           CredentialsProviderSharedPtr{credentials_provider}, context_,
                           Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, false, 5);
    if (use_unsigned_payload) {
      status = signer.signUnsignedPayload(headers, override_region);
    } else {
      status = signer.signEmptyPayload(headers, override_region);
    }
    EXPECT_TRUE(status.ok());

    EXPECT_EQ(fmt::format("AWS4-HMAC-SHA256 Credential=akid/20180102/{}/{}/aws4_request, "
                          "SignedHeaders=host;x-amz-content-sha256;x-amz-date, "
                          "Signature={}",
                          override_region.empty() ? "region" : override_region, service_name,
                          signature),
              headers.get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView());
    EXPECT_EQ(payload,
              headers.get(SigV4SignatureHeaders::get().ContentSha256)[0]->value().getStringView());
  }

  void expectQueryString(absl::string_view service_name,
                         Http::TestRequestHeaderMapImpl extra_headers,
                         absl::string_view signature_to_match, bool token_credentials,
                         const absl::string_view override_region = "") {
    auto* credentials_provider = new NiceMock<MockCredentialsProvider>();
    if (token_credentials) {
      EXPECT_CALL(*credentials_provider, getCredentials()).WillOnce(Return(token_credentials_));
    } else {
      EXPECT_CALL(*credentials_provider, getCredentials()).WillOnce(Return(credentials_));
    }

    SigV4SignerImpl signer(service_name, "region",
                           CredentialsProviderSharedPtr{credentials_provider}, context_,
                           Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true, 5);

    auto status = signer.signUnsignedPayload(extra_headers, override_region);
    EXPECT_TRUE(status.ok());
    auto query_parameters = Http::Utility::QueryParamsMulti::parseQueryString(
        extra_headers.Path()->value().getStringView());
    EXPECT_EQ(query_parameters.getFirstValue("X-Amz-Signature"), signature_to_match);
  }

  NiceMock<MockCredentialsProvider>* credentials_provider_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Http::RequestMessagePtr message_;
  SigV4SignerImpl signer_;
  Credentials credentials_;
  Credentials token_credentials_;
  absl::optional<std::string> region_;
};

// No authorization header should be present when the credentials are empty
TEST_F(SigV4SignerImplTest, AnonymousCredentials) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(Credentials()));
  auto status = signer_.sign(*message_);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(message_->headers().get(Http::CustomHeaders::get().Authorization).empty());
}

// HTTP :method header is required
TEST_F(SigV4SignerImplTest, MissingMethod) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  auto status = signer_.sign(*message_);
  EXPECT_EQ(status.message(), "Message is missing :method header");
  EXPECT_TRUE(message_->headers().get(Http::CustomHeaders::get().Authorization).empty());
}

// HTTP :path header is required
TEST_F(SigV4SignerImplTest, MissingPath) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  auto status = signer_.sign(*message_);
  EXPECT_EQ(status.message(), "Message is missing :path header");
  EXPECT_TRUE(message_->headers().get(Http::CustomHeaders::get().Authorization).empty());
}

// Verify that we replace, not duplicate or append to existing headers
TEST_F(SigV4SignerImplTest, DontDuplicateHeaders) {
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(token_credentials_));
  addMethod("GET");
  addPath("/");

  addHeader("authorization", "existing_value");
  addHeader("x-amz-security-token", "existing_value_2");
  addHeader("x-amz-date", "existing_value_3");

  auto status = signer_.sign(*message_);
  EXPECT_EQ(message_->headers().get(Http::CustomHeaders::get().Authorization).size(), 1);
  ENVOY_LOG_MISC(info, "authorization {}",
                 message_->headers()
                     .get(Http::CustomHeaders::get().Authorization)[0]
                     ->value()
                     .getStringView());
  ENVOY_LOG_MISC(info, "date {}",
                 message_->headers()
                     .get(Envoy::Http::LowerCaseString("x-amz-date"))[0]
                     ->value()
                     .getStringView());
  EXPECT_FALSE(absl::StrContains(
      message_->headers().get(Http::CustomHeaders::get().Authorization)[0]->value().getStringView(),
      "existing_value"));
  EXPECT_EQ(message_->headers().get(Envoy::Http::LowerCaseString("x-amz-security-token")).size(),
            1);
  EXPECT_FALSE(absl::StrContains(message_->headers()
                                     .get(Envoy::Http::LowerCaseString("x-amz-security-token"))[0]
                                     ->value()
                                     .getStringView(),
                                 "existing_value_2"));
  EXPECT_EQ(message_->headers().get(Envoy::Http::LowerCaseString("x-amz-date")).size(), 1);
  EXPECT_FALSE(absl::StrContains(message_->headers()
                                     .get(Envoy::Http::LowerCaseString("x-amz-date"))[0]
                                     ->value()
                                     .getStringView(),
                                 "existing_value_3"));
}

// Verify we sign the date header
TEST_F(SigV4SignerImplTest, SignDateHeader) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  auto status = signer_.sign(*message_);
  EXPECT_TRUE(status.ok());
  EXPECT_FALSE(message_->headers().get(SigV4SignatureHeaders::get().ContentSha256).empty());
  EXPECT_EQ("20180102T030400Z",
            message_->headers().get(SigV4SignatureHeaders::get().Date)[0]->value().getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date, "
            "Signature=4ee6aa9355259c18133f150b139ea9aeb7969c9408ad361b2151f50a516afe42",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify we sign the security token header if the token is present in the credentials
TEST_F(SigV4SignerImplTest, SignSecurityTokenHeader) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));

  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(token_credentials_));
  addMethod("GET");
  addPath("/");
  auto status = signer_.sign(*message_);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("token", message_->headers()
                         .get(SigV4SignatureHeaders::get().SecurityToken)[0]
                         ->value()
                         .getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date;x-amz-security-token, "
            "Signature=1d42526aabf7d8b6d7d33d9db43b03537300cc7e6bb2817e349749e0a08f5b5e",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify we sign the content header as the hashed empty string if the body is empty
TEST_F(SigV4SignerImplTest, SignEmptyContentHeader) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  auto status = signer_.sign(*message_, true);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(SigV4SignatureConstants::HashedEmptyString,
            message_->headers()
                .get(SigV4SignatureHeaders::get().ContentSha256)[0]
                ->value()
                .getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date, "
            "Signature=4ee6aa9355259c18133f150b139ea9aeb7969c9408ad361b2151f50a516afe42",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify we sign the content header correctly when we have a body
TEST_F(SigV4SignerImplTest, SignContentHeader) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("POST");
  addPath("/");
  setBody("test1234");
  auto status = signer_.sign(*message_, true);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("937e8d5fbb48bd4949536cd65b8d35c426b80d2f830c5c308e2cdec422ae2244",
            message_->headers()
                .get(SigV4SignatureHeaders::get().ContentSha256)[0]
                ->value()
                .getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date, "
            "Signature=4eab89c36f45f2032d6010ba1adab93f8510ddd6afe540821f3a05bb0253e27b",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify we sign the content header correctly when we have a body with region override
TEST_F(SigV4SignerImplTest, SignContentHeaderOverrideRegion) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("POST");
  addPath("/");
  setBody("test1234");
  auto status = signer_.sign(*message_, true, "region1");
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("937e8d5fbb48bd4949536cd65b8d35c426b80d2f830c5c308e2cdec422ae2244",
            message_->headers()
                .get(SigV4SignatureHeaders::get().ContentSha256)[0]
                ->value()
                .getStringView());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region1/service/aws4_request, "
            "SignedHeaders=x-amz-content-sha256;x-amz-date, "
            "Signature=fe8136ed21972d8618171e051f4023b7c06b85d61b4d4325be869846f404b399",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify we sign some extra headers
TEST_F(SigV4SignerImplTest, SignExtraHeaders) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));

  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  addHeader("a", "a_value");
  addHeader("b", "b_value");
  addHeader("c", "c_value");
  auto status = signer_.sign(*message_);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=a;b;c;x-amz-content-sha256;x-amz-date, "
            "Signature=0940025fcecfef5d7ee30e0a26a0957e116560e374878cd86ef4316c53ae9e81",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify signing a host header
TEST_F(SigV4SignerImplTest, SignHostHeader) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(credentials_));
  addMethod("GET");
  addPath("/");
  addHeader("host", "www.example.com");
  auto status = signer_.sign(*message_);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("AWS4-HMAC-SHA256 Credential=akid/20180102/region/service/aws4_request, "
            "SignedHeaders=host;x-amz-content-sha256;x-amz-date, "
            "Signature=d9fd9be575a254c924d843964b063d770181d938ae818f5b603ef0575a5ce2cd",
            message_->headers()
                .get(Http::CustomHeaders::get().Authorization)[0]
                ->value()
                .getStringView());
}

// Verify query string signing defaults to 5s
TEST_F(SigV4SignerImplTest, QueryStringDefault5s) {

  auto* credentials_provider = new NiceMock<MockCredentialsProvider>();
  Http::TestRequestHeaderMapImpl headers{};

  EXPECT_CALL(*credentials_provider, getCredentials()).WillOnce(Return(credentials_));

  headers.setMethod("GET");
  // Simple path, 1 extra header
  headers.setPath("/example/path");
  headers.addCopy(Http::LowerCaseString("host"), "example.service.zz");
  headers.addCopy("testheader", "value1");
  SigV4SignerImpl querysigner("service", "region",
                              CredentialsProviderSharedPtr{credentials_provider}, context_,
                              Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true);

  auto status = querysigner.signUnsignedPayload(headers);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(absl::StrContains(headers.getPathValue(), "X-Amz-Expires=5&"));
}

// Verify sigv4 and query parameters
TEST_F(SigV4SignerImplTest, QueryParameters) {

  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));

  Http::TestRequestHeaderMapImpl headers{};

  headers.setMethod("GET");
  // Simple path, 1 extra header
  headers.setPath("/example/path");
  headers.addCopy(Http::LowerCaseString("host"), "example.service.zz");
  headers.addCopy("testheader", "value1");
  expectQueryString("vpc-lattice-svcs", headers,
                    "5ab1a13f9d9623b4060507975ae2e656c5cd9dfffcd629a0f6702b1d2114b551", false);
  // Trivial path
  headers.setPath("/");
  expectQueryString("vpc-lattice-svcs", headers,

                    "b262c0878e531b09e0d8ce2b451fcd6ffedcfe30bedccd8c0d057243e5e1bdf1", false);
  // Additional query strings
  headers.setPath("/?query1=query1&query2=ZZZZZZZZZZ");
  expectQueryString("vpc-lattice-svcs", headers,
                    "17b38464e04c3e5d7b4caafd47fd376209087fdc18d08c0980e299accaa5aaf1", false);
  // Query strings and security token
  headers.setPath("/?query1=query1&query2=ZZZZZZZZZZ");
  expectQueryString("vpc-lattice-svcs", headers,
                    "539c3bf87477012a85ffa129e5db0fc6efcee5b758e3cf4fac393d319977d4ea", true);
}

// Verify signing headers for services.
TEST_F(SigV4SignerImplTest, SignHeadersByService) {
  // These original tests used 2018-01-02 03:04:00 rather than 03:04:05
  time_system_.setSystemTime(std::chrono::milliseconds(1514862240000));

  expectSignHeaders("s3", "d97cae067345792b78d2bad746f25c729b9eb4701127e13a7c80398f8216a167",
                    SigV4SignatureConstants::UnsignedPayload, true);
  expectSignHeaders("service", "d9fd9be575a254c924d843964b063d770181d938ae818f5b603ef0575a5ce2cd",
                    SigV4SignatureConstants::HashedEmptyString, false);
  expectSignHeaders("es", "0fd9c974bb2ad16c8d8a314dca4f6db151d32cbd04748d9c018afee2a685a02e",
                    SigV4SignatureConstants::UnsignedPayload, true);
  expectSignHeaders("glacier", "8d1f241d77c64cda57b042cd312180f16e98dbd7a96e5545681430f8dbde45a0",
                    SigV4SignatureConstants::UnsignedPayload, true);

  // with override region
  expectSignHeaders("s3", "70b80eaedfe73d9cf18a9d2f786f02a7dab013780a8cdc42a7c819a27bfd943c",
                    SigV4SignatureConstants::UnsignedPayload, true, "region1");
  expectSignHeaders("service", "297ca067391806a1e3cdb25723082063d0bf66a6472b902dd986d540a2058a13",
                    SigV4SignatureConstants::HashedEmptyString, false, "region1");
  expectSignHeaders("es", "cec43f0777c0d4cb2f3799a5c755dc4c3b893c23e268c1bd4e34f770fba3c1ca",
                    SigV4SignatureConstants::UnsignedPayload, true, "region1");
  expectSignHeaders("glacier", "0792940297330f2930dc1c18d0b99c0b85429865c09e836f5c086f7f182e2809",
                    SigV4SignatureConstants::UnsignedPayload, true, "region1");
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
