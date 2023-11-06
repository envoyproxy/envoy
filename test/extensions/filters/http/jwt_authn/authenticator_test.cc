#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/jwks_fetcher.h"
#include "source/extensions/filters/http/jwt_authn/authenticator.h"
#include "source/extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/common/mock.h"
#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;
using Envoy::Extensions::HttpFilters::Common::JwksFetcher;
using Envoy::Extensions::HttpFilters::Common::JwksFetcherPtr;
using Envoy::Extensions::HttpFilters::Common::MockJwksFetcher;
using ::google::jwt_verify::Jwks;
using ::google::jwt_verify::Status;
using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class AuthenticatorTest : public testing::Test {
public:
  void SetUp() override {
    TestUtility::loadFromYaml(ExampleConfig, proto_config_);
    createAuthenticator();
  }

  void createAuthenticator(
      ::google::jwt_verify::CheckAudience* check_audience = nullptr,
      const absl::optional<std::string>& provider = absl::make_optional<std::string>(ProviderName),
      bool allow_failed = false, bool allow_missing = false) {
    filter_config_ = std::make_unique<FilterConfigImpl>(proto_config_, "", mock_factory_ctx_);
    raw_fetcher_ = new MockJwksFetcher;
    fetcher_.reset(raw_fetcher_);
    auth_ = Authenticator::create(
        check_audience, provider, allow_failed, allow_missing, filter_config_->getJwksCache(),
        filter_config_->cm(),
        [this](Upstream::ClusterManager&, const RemoteJwks&) { return std::move(fetcher_); },
        filter_config_->timeSource());
    jwks_ = Jwks::createFrom(PublicKey, Jwks::JWKS);
    EXPECT_TRUE(jwks_->getStatus() == Status::Ok);
  }

  void expectVerifyStatus(Status expected_status, Http::RequestHeaderMap& headers,
                          bool expect_clear_route = false) {
    std::function<void(const Status&)> on_complete_cb = [&expected_status](const Status& status) {
      ASSERT_EQ(status, expected_status);
    };
    auto set_extracted_jwt_data_cb = [this](const std::string& name,
                                            const ProtobufWkt::Struct& extracted_data) {
      this->addExtractedData(name, extracted_data);
    };
    initTokenExtractor();
    auto tokens = extractor_->extract(headers);
    bool clear_route = false;
    auth_->verify(headers, parent_span_, std::move(tokens), std::move(set_extracted_jwt_data_cb),
                  std::move(on_complete_cb), [&clear_route] { clear_route = true; });
    EXPECT_EQ(expect_clear_route, clear_route);
  }

  void initTokenExtractor() {
    JwtProviderList providers;
    for (const auto& it : proto_config_.providers()) {
      providers.emplace_back(&it.second);
    }
    extractor_ = Extractor::create(providers);
  }

  // This is like ContextImpl::addExtractedData in
  // source/extensions/filters/http/jwt_authn/verifier.cc.
  void addExtractedData(const std::string& name, const ProtobufWkt::Struct& extracted_data) {
    *(*out_extracted_data_.mutable_fields())[name].mutable_struct_value() = extracted_data;
  }

  JwtAuthentication proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  ExtractorConstPtr extractor_;
  std::shared_ptr<FilterConfigImpl> filter_config_;
  MockJwksFetcher* raw_fetcher_;
  JwksFetcherPtr fetcher_;
  AuthenticatorPtr auth_;
  ::google::jwt_verify::JwksPtr jwks_;
  ProtobufWkt::Struct out_extracted_data_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

// This test validates a good JWT authentication with a remote Jwks.
// It also verifies Jwks cache with 10 JWT authentications, but only one Jwks fetch.
TEST_F(AuthenticatorTest, TestOkJWTandCache) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Test OK pubkey and its cache
  for (int i = 0; i < 10; i++) {
    Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};

    expectVerifyStatus(Status::Ok, headers);

    EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
    // Verify the token is removed.
    EXPECT_FALSE(headers.has(Http::CustomHeaders::get().Authorization));
  }

  EXPECT_EQ(1U, filter_config_->stats().jwks_fetch_success_.value());
  EXPECT_EQ(0U, filter_config_->stats().jwks_fetch_failed_.value());
}

TEST_F(AuthenticatorTest, TestCompletePaddingInJwtPayload) {
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_pad_forward_payload_header(
      true);
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValueWithPadding);
}

// This test verifies whether the claim is successfully added to header or not
TEST_F(AuthenticatorTest, TestClaimToHeader) {
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(NestedGoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  EXPECT_EQ(headers.get_("x-jwt-claim-sub"), "test@example.com");
  EXPECT_EQ(headers.get_("x-jwt-claim-nested"), "value1");
  EXPECT_EQ(headers.get_("x-jwt-bool-claim"), "true");
  EXPECT_EQ(headers.get_("x-jwt-int-claim"), "9999");

  // This check verifies whether the claim with non-primitive type are
  // successfully serialized and added to headers.
  std::string expected_json = "[\"str1\",\"str2\"]";

  ASSERT_EQ(headers.get_("x-jwt-claim-object-key"),
            Envoy::Base64::encode(expected_json.data(), expected_json.size()));
}

// This test verifies whether the claim is successfully added to header or not
TEST_F(AuthenticatorTest, TestClaimToHeaderWithClearRouteCache) {
  TestUtility::loadFromYaml(ClaimToHeadersConfig, proto_config_);
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  {
    Http::TestRequestHeaderMapImpl headers{
        {"Authorization", "Bearer " + std::string(NestedGoodToken)}};
    expectVerifyStatus(Status::Ok, headers, true);
    EXPECT_EQ(headers.get_("x-jwt-claim-nested"), "value1");
  }

  {
    Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
    expectVerifyStatus(Status::Ok, headers, false);
    EXPECT_FALSE(headers.has("x-jwt-claim-nested"));
  }
}

// This test verifies when wrong claim is passed in claim_to_headers
TEST_F(AuthenticatorTest, TestClaimToHeaderWithHeaderReplace) {
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(NestedGoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  EXPECT_EQ(headers.get_("x-jwt-claim-sub"), "test@example.com");
  EXPECT_EQ(headers.get_("x-jwt-claim-nested"), "value1");
  EXPECT_FALSE(headers.has("x-jwt-claim-nested-wrong"));
  EXPECT_FALSE(headers.has("x-jwt-unsupported-type-claim"));
}

// This test verifies the Jwt is forwarded if "forward" flag is set.
TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Config forward_jwt flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_forward(true);
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Test OK pubkey and its cache
  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Verify the token is NOT removed.
  EXPECT_TRUE(headers.has(Http::CustomHeaders::get().Authorization));

  // Payload is not set by default.
  EXPECT_TRUE(out_extracted_data_.fields().empty());

  EXPECT_EQ(1U, filter_config_->stats().jwks_fetch_success_.value());
  EXPECT_EQ(0U, filter_config_->stats().jwks_fetch_failed_.value());
}

// This test verifies the JWT payload is set.
TEST_F(AuthenticatorTest, TestSetPayload) {
  // Config payload_in_metadata flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Test OK pubkey and its cache
  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  ProtobufWkt::Value expected_payload;
  TestUtility::loadFromJson(ExpectedPayloadJSON, expected_payload);
  EXPECT_TRUE(
      TestUtility::protoEqual(expected_payload, out_extracted_data_.fields().at("my_payload")));
}

// This test verifies the JWT payload is set.
TEST_F(AuthenticatorTest, TestSetPayloadWithSpaces) {
  // Config payload_in_metadata flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  auto* normalize_payload = (*proto_config_.mutable_providers())[std::string(ProviderName)]
                                .mutable_normalize_payload_in_metadata();
  normalize_payload->add_space_delimited_claims("scope");
  normalize_payload->add_space_delimited_claims("test_string");
  normalize_payload->add_space_delimited_claims("test_num");

  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Test OK pubkey and its cache
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(GoodTokenWithSpaces)}};

  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  ProtobufWkt::Value expected_payload;
  TestUtility::loadFromJson(ExpectedPayloadJSONWithSpaces, expected_payload);
  EXPECT_TRUE(
      TestUtility::protoEqual(expected_payload, out_extracted_data_.fields().at("my_payload")));
}

// This test verifies setting only the extracted header to metadata.
TEST_F(AuthenticatorTest, TestSetHeader) {
  // Set the extracted header to metadata.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_header_in_metadata(
      "my_header");

  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Expect to have a valid JWT.
  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  // We should expect empty JWT payload.
  ProtobufWkt::Value expected_payload;
  TestUtility::loadFromJson(ExpectedHeaderJSON, expected_payload);
  EXPECT_TRUE(
      TestUtility::protoEqual(expected_payload, out_extracted_data_.fields().at("my_header")));
}

// This test verifies writing JwtExpired status in metadata
TEST_F(AuthenticatorTest, TestSetExpiredJwtToGetStatus) {
  // Config provider and failed status in metadata
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.set_failed_status_in_metadata("jwt-failure-reason");
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Test JwtExpired pubkey and its cache
  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(ExpiredToken)}};

  expectVerifyStatus(Status::JwtExpired, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  EXPECT_EQ(enumToInt(Status::JwtExpired), out_extracted_data_.fields()
                                               .at("jwt-failure-reason")
                                               .struct_value()
                                               .fields()
                                               .at("code")
                                               .number_value());

  EXPECT_EQ(google::jwt_verify::getStatusString(Status::JwtExpired), out_extracted_data_.fields()
                                                                         .at("jwt-failure-reason")
                                                                         .struct_value()
                                                                         .fields()
                                                                         .at("message")
                                                                         .string_value());
}

// This test verifies writing InvalidAudience status into metadata
// with allow_failed=true and allow_missing=true
TEST_F(AuthenticatorTest, TestSetInvalidJwtInvalidAudienceToGetStatus) {
  // Config provider and failed status in metadata
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.set_failed_status_in_metadata("jwt-failure-reason");
  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/true, /*allow_missing=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Test JwtAudienceNotAllowed pubkey and its cache
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  // VerifyStatus is OK since allow_failed=true
  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  EXPECT_EQ(enumToInt(Status::JwtAudienceNotAllowed), out_extracted_data_.fields()
                                                          .at("jwt-failure-reason")
                                                          .struct_value()
                                                          .fields()
                                                          .at("code")
                                                          .number_value());

  EXPECT_EQ(google::jwt_verify::getStatusString(Status::JwtAudienceNotAllowed),
            out_extracted_data_.fields()
                .at("jwt-failure-reason")
                .struct_value()
                .fields()
                .at("message")
                .string_value());
}

// This test verifies jwt missing status in failed status in metadata with allow missing or failed
TEST_F(AuthenticatorTest, TestSetMissingJwtToGetStatus) {
  // Config provider and failed status in metadata
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.set_failed_status_in_metadata("jwt-failure-reason");
  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/true, /*allow_missing=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Test JwtMissed
  Http::TestRequestHeaderMapImpl headers{};

  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  EXPECT_EQ(enumToInt(Status::JwtMissed), out_extracted_data_.fields()
                                              .at("jwt-failure-reason")
                                              .struct_value()
                                              .fields()
                                              .at("code")
                                              .number_value());

  EXPECT_EQ(google::jwt_verify::getStatusString(Status::JwtMissed), out_extracted_data_.fields()
                                                                        .at("jwt-failure-reason")
                                                                        .struct_value()
                                                                        .fields()
                                                                        .at("message")
                                                                        .string_value());
}

// This test verifies two tokens, one is good another is with invalidAudience
// with allow_failed=true and allow_missing=true
// expect writing invalidAudience to metadata
TEST_F(AuthenticatorTest, TestSetInvalidAndValidJwtToGetStatus) {
  // Config provider and failed status in metadata
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.set_failed_status_in_metadata("jwt-failure-reason");

  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/true, /*allow_missing=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Expect to have a valid JWT.
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/foo?access_token=" + std::string(InvalidAudToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  EXPECT_EQ(enumToInt(Status::JwtAudienceNotAllowed), out_extracted_data_.fields()
                                                          .at("jwt-failure-reason")
                                                          .struct_value()
                                                          .fields()
                                                          .at("code")
                                                          .number_value());

  EXPECT_EQ(google::jwt_verify::getStatusString(Status::JwtAudienceNotAllowed),
            out_extracted_data_.fields()
                .at("jwt-failure-reason")
                .struct_value()
                .fields()
                .at("message")
                .string_value());
}

// This test verifies two bad tokens, one is expired and another is with invalidAudience
// with allow_failed=true and allow_missing=true
// expect writing JwtExpired to metadata as it is the last one.
TEST_F(AuthenticatorTest, TestSetTwoInvalidJwtToGetStatus) {
  // Config provider and failed status in metadata
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.set_failed_status_in_metadata("jwt-failure-reason");

  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/true, /*allow_missing=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Expect to have a valid JWT.
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(ExpiredToken)},
      {":path", "/foo?access_token=" + std::string(InvalidAudToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Only one field is set.
  EXPECT_EQ(1, out_extracted_data_.fields().size());

  EXPECT_EQ(enumToInt(Status::JwtExpired), out_extracted_data_.fields()
                                               .at("jwt-failure-reason")
                                               .struct_value()
                                               .fields()
                                               .at("code")
                                               .number_value());

  EXPECT_EQ(google::jwt_verify::getStatusString(Status::JwtExpired), out_extracted_data_.fields()
                                                                         .at("jwt-failure-reason")
                                                                         .struct_value()
                                                                         .fields()
                                                                         .at("message")
                                                                         .string_value());
}

// This test set two providers and send request without jwt
// with allow_failed=true and allow_missing=true
// expect jwt missing status shouldn't write to metadata.
TEST_F(AuthenticatorTest, TestSetTwoProvidersJwtMissing) {
  // Config provider and failed status in metadata
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.set_failed_status_in_metadata("jwt-failure-reason");

  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/true, /*allow_missing=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Test JwtMissed
  Http::TestRequestHeaderMapImpl headers{};

  expectVerifyStatus(Status::Ok, headers);

  // Status isn't set in this field.
  EXPECT_EQ(0, out_extracted_data_.fields().size());
}

// This test verifies setting the extracted payload and header to metadata.
TEST_F(AuthenticatorTest, TestSetPayloadAndHeader) {
  // Set the extracted payload and header to metadata.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_header_in_metadata(
      "my_header");

  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Expect to have a valid JWT.
  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Payload and header are set.
  EXPECT_EQ(2, out_extracted_data_.fields().size());

  // We should expect both JWT payload and header are set.
  ProtobufWkt::Value expected_payload;
  TestUtility::loadFromJson(ExpectedPayloadJSON, expected_payload);
  EXPECT_TRUE(
      TestUtility::protoEqual(expected_payload, out_extracted_data_.fields().at("my_payload")));

  ProtobufWkt::Value expected_header;
  TestUtility::loadFromJson(ExpectedHeaderJSON, expected_header);
  EXPECT_TRUE(
      TestUtility::protoEqual(expected_header, out_extracted_data_.fields().at("my_header")));
}

// This test verifies the Jwt with non existing kid
TEST_F(AuthenticatorTest, TestJwtWithNonExistKid) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Test OK pubkey and its cache
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(NonExistKidToken)}};

  expectVerifyStatus(Status::JwtVerificationFail, headers);
}

// Jwt "iss" is "other.com", it doesn't match "issuer" specified in JwtProvider.
// The verification fails with JwtUnknownIssuer.
TEST_F(AuthenticatorTest, TestWrongIssuer) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);
  // Token with other issuer should fail.
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(OtherGoodToken)}};
  expectVerifyStatus(Status::JwtUnknownIssuer, headers);

  EXPECT_EQ(0U, filter_config_->stats().jwks_fetch_success_.value());
  EXPECT_EQ(0U, filter_config_->stats().jwks_fetch_failed_.value());
}

// Jwt "iss" is "other.com", "issuer" in JwtProvider is not specified,
// authenticator has a valid provider. The verification is OK.
TEST_F(AuthenticatorTest, TestWrongIssuerOKWithProvider) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_issuer();
  provider.clear_audiences();
  createAuthenticator();

  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Token with other issuer is OK since "issuer" is not specified in the provider.
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(OtherGoodToken)}};
  expectVerifyStatus(Status::Ok, headers);
}

// Jwt "iss" is "other.com", "issuer" in JwtProvider is not specified,
// authenticator doesn't have a valid provider. The verification is OK.
// When "allow_missing" or "allow_failed" is used, authenticator doesn't have a valid provider.
TEST_F(AuthenticatorTest, TestWrongIssuerOKWithoutProvider) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_issuer();
  provider.clear_audiences();
  // use authenticator without a valid provider
  createAuthenticator(nullptr, absl::nullopt);

  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  // Token with other issuer is OK since issuer is not specified in the provider.
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(OtherGoodToken)}};
  expectVerifyStatus(Status::Ok, headers);
}

// Not "iss" in Jwt, "issuer" in JwtProvider is specified,
// authenticator has a valid provider. The verification fails.
TEST_F(AuthenticatorTest, TestJwtWithoutIssWithValidProvider) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(ES256WithoutIssToken)}};
  expectVerifyStatus(Status::JwtUnknownIssuer, headers);
}

// Not "iss" in Jwt, "issuer" in JwtProvider is specified,
// authenticator doesn't have a valid provider.
// It needs to find the first JwtProvider without "issuer" specified.
// The verification fails with JwtUnknownIssuer.
// When "allow_missing" or "allow_failed" is used, authenticator doesn't have a valid provider.
TEST_F(AuthenticatorTest, TestJwtWithoutIssWithoutValidProvider) {
  createAuthenticator(nullptr, absl::nullopt);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(ES256WithoutIssToken)}};
  expectVerifyStatus(Status::JwtUnknownIssuer, headers);
}

// Not "iss" in Jwt, "issuer" in JwtProvider is not specified,
// authenticator has a valid provider. The verification is OK
TEST_F(AuthenticatorTest, TestJwtWithoutIssWithValidProviderNotIssuer) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_issuer();
  createAuthenticator();

  jwks_ = Jwks::createFrom(ES256PublicKey, Jwks::JWKS);
  EXPECT_TRUE(jwks_->getStatus() == Status::Ok);

  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(ES256WithoutIssToken)}};

  expectVerifyStatus(Status::Ok, headers);
}

// Not "iss" in Jwt, "issuer" in JwtProvider is not specified,
// authenticator doesn't have a valid provider. The verification is OK
// When "allow_missing" or "allow_failed" is used, authenticator doesn't have a valid provider.
TEST_F(AuthenticatorTest, TestJwtWithoutIssWithoutValidProviderNotIssuer) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_issuer();
  createAuthenticator(nullptr, absl::nullopt);

  jwks_ = Jwks::createFrom(ES256PublicKey, Jwks::JWKS);
  EXPECT_TRUE(jwks_->getStatus() == Status::Ok);

  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(ES256WithoutIssToken)}};
  expectVerifyStatus(Status::Ok, headers);
}

// This test verifies if Jwt is missing, proper status is called.
TEST_F(AuthenticatorTest, TestMissedJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Empty headers.
  Http::TestRequestHeaderMapImpl headers{};

  expectVerifyStatus(Status::JwtMissed, headers);
}

// Test multiple tokens; the one from query parameter is bad, verification should fail.
TEST_F(AuthenticatorTest, TestMultipleJWTOneBadFromQuery) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _));

  // headers with multiple tokens: one good, one bad
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/foo?access_token=" + std::string(NonExistKidToken)},
  };

  expectVerifyStatus(Status::JwtVerificationFail, headers);
}

// Test multiple tokens; the one from header is bad, verification should fail.
TEST_F(AuthenticatorTest, TestMultipleJWTOneBadFromHeader) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _));

  // headers with multiple tokens: one good, one bad
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(NonExistKidToken)},
      {":path", "/foo?access_token=" + std::string(GoodToken)},
  };

  expectVerifyStatus(Status::JwtVerificationFail, headers);
}

// Test multiple tokens; all are good, verification is ok.
TEST_F(AuthenticatorTest, TestMultipleJWTAllGood) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _));

  // headers with multiple tokens: all are good
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/foo?access_token=" + std::string(GoodToken)},
  };

  expectVerifyStatus(Status::Ok, headers);
}

// Test multiple tokens; one of them is bad and allow_failed, verification is ok.
TEST_F(AuthenticatorTest, TestMultipleJWTOneBadAllowFails) {
  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/true, /*all_missing=*/false);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _));

  // headers with multiple tokens: one good, one bad
  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/foo?access_token=" + std::string(NonExistKidToken)},
  };

  expectVerifyStatus(Status::Ok, headers);
}

// Test empty header and allow_missing, verification is ok.
TEST_F(AuthenticatorTest, TestAllowMissingWithEmptyHeader) {
  createAuthenticator(nullptr, absl::make_optional<std::string>(ProviderName),
                      /*allow_failed=*/false, /*all_missing=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Empty headers
  Http::TestRequestHeaderMapImpl headers{};

  expectVerifyStatus(Status::Ok, headers);
}

// This test verifies if Jwt is invalid, JwtBadFormat status is returned.
TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  std::string token = "invalidToken";
  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + token}};
  expectVerifyStatus(Status::JwtBadFormat, headers);
}

// This test verifies if Authorization header has invalid prefix, JwtMissed status is returned
TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer-invalid"}};
  expectVerifyStatus(Status::JwtMissed, headers);
}

// This test verifies when a JWT is non-expiring without audience specified, JwtAudienceNotAllowed
// is returned.
TEST_F(AuthenticatorTest, TestNonExpiringJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.thread_local_cluster_, httpAsyncClient()).Times(0);

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(NonExpiringToken)}};
  expectVerifyStatus(Status::JwtAudienceNotAllowed, headers);
}

// This test verifies when a JWT is expired, JwtExpired status is returned.
TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  expectVerifyStatus(Status::JwtExpired, headers);
}

// This test verifies when a JWT is expired but with a big clock skew.
TEST_F(AuthenticatorTest, TestExpiredJWTWithABigClockSkew) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  // Token is expired at 1205005587, but add clock skew at another 1205005587.
  provider.set_clock_skew_seconds(1205005587);
  createAuthenticator();

  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  expectVerifyStatus(Status::Ok, headers);
}

// This test verifies when a JWT is not yet valid, JwtNotYetValid status is returned.
TEST_F(AuthenticatorTest, TestNotYetValidJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(NotYetValidToken)}};
  expectVerifyStatus(Status::JwtNotYetValid, headers);
}

// This test verifies when an inline JWKS is misconfigured, JwksNoValidKeys is returns
TEST_F(AuthenticatorTest, TestInvalidLocalJwks) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_remote_jwks();
  provider.mutable_local_jwks()->set_inline_string("invalid");
  createAuthenticator();

  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwksNoValidKeys, headers);
}

// This test verifies when a JWT is with invalid audience, JwtAudienceNotAllowed is returned.
TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  expectVerifyStatus(Status::JwtAudienceNotAllowed, headers);
}

// This test verifies when Jwt issuer is not configured, JwtUnknownIssuer is returned.
TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_issuer("other_issuer");
  createAuthenticator();

  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwtUnknownIssuer, headers);
}

// This test verifies that when Jwks fetching fails, JwksFetchFail status is returned.
TEST_F(AuthenticatorTest, TestPubkeyFetchFail) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks);
      }));

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwksFetchFail, headers);

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "401"}}}));
}

// This test verifies when a Jwks fetching is not completed yet, but onDestroy() is called,
// onComplete() callback should not be called, but internal request->cancel() should be called.
// Most importantly, no crash.
TEST_F(AuthenticatorTest, TestOnDestroy) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _));

  // Cancel is called once.
  EXPECT_CALL(*raw_fetcher_, cancel());

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  initTokenExtractor();
  auto tokens = extractor_->extract(headers);
  // callback should not be called.
  std::function<void(const Status&)> on_complete_cb = [](const Status&) { FAIL(); };
  auth_->verify(headers, parent_span_, std::move(tokens), nullptr, std::move(on_complete_cb),
                nullptr);

  // Destroy the authenticating process.
  auth_->onDestroy();
}

// This test verifies if "forward_payload_header" is empty, payload is not forwarded.
TEST_F(AuthenticatorTest, TestNoForwardPayloadHeader) {
  // In this config, there is no forward_payload_header
  auto& provider0 = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_forward_payload_header();
  createAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::Ok, headers);

  // Test when forward_payload_header is not set, the output should NOT
  // contain the sec-istio-auth-userinfo header.
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

// This test verifies that allow failed authenticator will verify all tokens.
TEST_F(AuthenticatorTest, TestAllowFailedMultipleTokens) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  std::vector<std::string> names = {"a", "b", "c"};
  for (const auto& it : names) {
    auto header = provider.add_from_headers();
    header->set_name(it);
    header->set_value_prefix("Bearer ");
  }

  createAuthenticator(nullptr, absl::nullopt, /*allow_failed=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers1{
      {"a", "Bearer " + std::string(ExpiredToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(InvalidAudToken)},
      {":path", "/"},
  };
  expectVerifyStatus(Status::Ok, headers1);

  EXPECT_TRUE(headers1.has("a"));
  EXPECT_FALSE(headers1.has("b"));
  EXPECT_TRUE(headers1.has("c"));

  Http::TestRequestHeaderMapImpl headers2{
      {"a", "Bearer " + std::string(GoodToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(GoodToken)},
      {":path", "/"},
  };
  expectVerifyStatus(Status::Ok, headers2);

  EXPECT_FALSE(headers2.has("a"));
  EXPECT_FALSE(headers2.has("b"));
  EXPECT_FALSE(headers2.has("c"));
}

// This test verifies that allow failed authenticator will verify all tokens.
TEST_F(AuthenticatorTest, TestAllowFailedMultipleIssuers) {
  auto& provider = (*proto_config_.mutable_providers())["other_provider"];
  provider.set_issuer("https://other.com");
  provider.add_audiences("other_service");
  auto& uri = *provider.mutable_remote_jwks()->mutable_http_uri();
  uri.set_uri("https://pubkey_server/pubkey_path");
  uri.set_cluster("pubkey_cluster");
  auto header = provider.add_from_headers();
  header->set_name("expired-auth");
  header->set_value_prefix("Bearer ");
  header = provider.add_from_headers();
  header->set_name("other-auth");
  header->set_value_prefix("Bearer ");

  createAuthenticator(nullptr, absl::nullopt, /*allow_failed=*/true);
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        ::google::jwt_verify::JwksPtr jwks = Jwks::createFrom(PublicKey, Jwks::JWKS);
        EXPECT_TRUE(jwks->getStatus() == Status::Ok);
        receiver.onJwksSuccess(std::move(jwks));
      }));

  Http::TestRequestHeaderMapImpl headers{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"expired-auth", "Bearer " + std::string(ExpiredToken)},
      {"other-auth", "Bearer " + std::string(OtherGoodToken)},
      {":path", "/"},
  };
  expectVerifyStatus(Status::Ok, headers);

  EXPECT_FALSE(headers.has("Authorization"));
  EXPECT_TRUE(headers.has("expired-auth"));
  EXPECT_FALSE(headers.has("other-auth"));
}

// Test checks that supplying a CheckAudience to auth will override the one in JwksCache.
TEST_F(AuthenticatorTest, TestCustomCheckAudience) {
  auto check_audience = std::make_unique<::google::jwt_verify::CheckAudience>(
      std::vector<std::string>{"invalid_service"});
  createAuthenticator(check_audience.get());
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([this](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        receiver.onJwksSuccess(std::move(jwks_));
      }));

  Http::TestRequestHeaderMapImpl headers1{
      {"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  expectVerifyStatus(Status::Ok, headers1);

  Http::TestRequestHeaderMapImpl headers2{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwtAudienceNotAllowed, headers2);
}

// This test verifies that when invalid JWKS is fetched, an JWKS error status is returned.
TEST_F(AuthenticatorTest, TestInvalidPubkeyKey) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke([](Tracing::Span&, JwksFetcher::JwksReceiver& receiver) {
        auto jwks = Jwks::createFrom(PublicKey, Jwks::PEM);
        receiver.onJwksSuccess(std::move(jwks));
      }));

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwksPemBadBase64, headers);
}

class AuthenticatorJwtCacheTest : public testing::Test {
public:
  void SetUp() override {
    jwks_ = Jwks::createFrom(PublicKey, Jwks::JWKS);
    extractor_ = Extractor::create(jwks_cache_.jwks_data_.jwt_provider_);
    // Not to use jwks_fetcher, mocked that JwksObj already has Jwks
    EXPECT_CALL(jwks_cache_.jwks_data_, getJwksObj()).WillRepeatedly(Return(jwks_.get()));
    EXPECT_CALL(mock_fetcher_, Call(_, _)).Times(0);
  }

  void createAuthenticator(const absl::optional<std::string>& provider) {
    auth_ = Authenticator::create(nullptr, provider, false, false, jwks_cache_, cm_,
                                  mock_fetcher_.AsStdFunction(), time_system_);
  }

  void expectVerifyStatus(Status expected_status, Http::RequestHeaderMap& headers) {
    std::function<void(const Status&)> on_complete_cb = [&expected_status](const Status& status) {
      ASSERT_EQ(status, expected_status);
    };
    auto set_extracted_jwt_data_cb = [this](const std::string& name,
                                            const ProtobufWkt::Struct& extracted_data) {
      out_name_ = name;
      out_extracted_data_ = extracted_data;
    };
    auto tokens = extractor_->extract(headers);
    auth_->verify(headers, parent_span_, std::move(tokens), set_extracted_jwt_data_cb,
                  on_complete_cb, nullptr);
  }

  ::google::jwt_verify::JwksPtr jwks_;
  NiceMock<MockJwksCache> jwks_cache_;
  MockFunction<Common::JwksFetcherPtr(Upstream::ClusterManager&, const RemoteJwks&)> mock_fetcher_;
  AuthenticatorPtr auth_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::SimulatedTimeSystem time_system_;
  ExtractorConstPtr extractor_;
  NiceMock<Tracing::MockSpan> parent_span_;
  std::string out_name_;
  ProtobufWkt::Struct out_extracted_data_;
};

TEST_F(AuthenticatorJwtCacheTest, TestNonProvider) {
  createAuthenticator(absl::nullopt);

  // For invalid provider, jwt_cache is not called.
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, lookup(_)).Times(0);
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, insert(GoodToken, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::Ok, headers);
}

TEST_F(AuthenticatorJwtCacheTest, TestCacheMissGoodToken) {
  createAuthenticator("provider");

  // jwt_cache miss: lookup return nullptr
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, lookup(_)).WillOnce(Return(nullptr));
  // jwt_cache insert is called for a good jwt.
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, insert(GoodToken, _));

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::Ok, headers);
}

TEST_F(AuthenticatorJwtCacheTest, TestCacheMissExpiredToken) {
  createAuthenticator("provider");

  // jwt_cache miss: lookup return nullptr
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, lookup(_)).WillOnce(Return(nullptr));
  // jwt_cache insert is not called for a bad Jwt
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, insert(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  expectVerifyStatus(Status::JwtExpired, headers);
}

TEST_F(AuthenticatorJwtCacheTest, TestCacheHit) {
  jwks_cache_.jwks_data_.jwt_provider_.set_forward_payload_header("jwt-payload");
  jwks_cache_.jwks_data_.jwt_provider_.set_forward(true);
  jwks_cache_.jwks_data_.jwt_provider_.set_payload_in_metadata("my_payload");

  createAuthenticator("provider");

  ::google::jwt_verify::Jwt cached_jwt;
  cached_jwt.parseFromString(GoodToken);
  // jwt_cache hit: lookup return a cached jwt.
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, lookup(_)).WillOnce(Return(&cached_jwt));
  // jwt_cache insert is not called.
  EXPECT_CALL(jwks_cache_.jwks_data_.jwt_cache_, insert(_, _)).Times(0);

  Http::TestRequestHeaderMapImpl headers{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::Ok, headers);

  // Verify post processing of a good Jwt with a cache hit.
  EXPECT_EQ(headers.get_("jwt-payload"), ExpectedPayloadValue);
  // Verify the token is not removed.
  EXPECT_TRUE(headers.has(Http::CustomHeaders::get().Authorization));

  // Payload is set
  EXPECT_EQ(out_name_, "my_payload");

  ProtobufWkt::Struct expected_payload;
  TestUtility::loadFromJson(ExpectedPayloadJSON, expected_payload);
  EXPECT_TRUE(TestUtility::protoEqual(out_extracted_data_, expected_payload));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
