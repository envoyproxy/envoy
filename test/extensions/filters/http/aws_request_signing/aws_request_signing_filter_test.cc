#include "envoy/http/filter.h"

#include "source/extensions/common/aws/signer.h"
#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {
namespace {

using Common::Aws::MockSigner;
using ::testing::An;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

class MockFilterConfig : public FilterConfig {
public:
  MockFilterConfig() {
    signer_ = std::make_shared<StrictMock<MockSigner>>();
    credentials_provider_ = std::make_shared<Common::Aws::MockCredentialsProvider>();
  }

  Common::Aws::Signer& signer() override { return *signer_; }
  FilterStats& stats() override { return stats_; }
  const std::string& hostRewrite() const override { return host_rewrite_; }
  bool useUnsignedPayload() const override { return use_unsigned_payload_; }
  Envoy::Extensions::Common::Aws::CredentialsProviderSharedPtr credentialsProvider() override {
    return credentials_provider_;
  }
  std::shared_ptr<Common::Aws::MockSigner> signer_;
  Stats::IsolatedStoreImpl stats_store_;
  FilterStats stats_{Filter::generateStats("test", *stats_store_.rootScope())};
  std::string host_rewrite_;
  bool use_unsigned_payload_;
  std::shared_ptr<Envoy::Extensions::Common::Aws::MockCredentialsProvider> credentials_provider_;
};

class AwsRequestSigningFilterTest : public testing::Test {
public:
  AwsRequestSigningFilterTest() : credentials_(Common::Aws::Credentials("akid", "skid")){};
  void setup() {
    filter_config_ = std::make_shared<MockFilterConfig>();
    filter_config_->use_unsigned_payload_ = false;
    filter_ = std::make_unique<Filter>(filter_config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void expectCredentials(bool pending = false) {
    EXPECT_CALL(*(filter_config_->credentials_provider_), credentialsPending(_))
        .WillRepeatedly(Return(pending));
    EXPECT_CALL(*(filter_config_->credentials_provider_), getCredentials())
        .WillRepeatedly(Return(credentials_));
  }

  std::shared_ptr<MockFilterConfig> filter_config_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Common::Aws::Credentials credentials_;
};

// Verify filter functionality when signing works for header only request.
TEST_F(AwsRequestSigningFilterTest, SignSucceeds) {
  setup();
  expectCredentials();
  EXPECT_CALL(*(filter_config_->signer_),
              signEmptyPayload(An<Http::RequestHeaderMap&>(), An<Common::Aws::Credentials>(),
                               An<absl::string_view>()));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_added_.value());
}

// Verify decodeHeaders signs when use_unsigned_payload is true and end_stream is false.
TEST_F(AwsRequestSigningFilterTest, DecodeHeadersSignsUnsignedPayload) {
  setup();
  expectCredentials();

  filter_config_->use_unsigned_payload_ = true;
  EXPECT_CALL(*(filter_config_->signer_),
              signUnsignedPayload(An<Http::RequestHeaderMap&>(), An<Common::Aws::Credentials>(),
                                  An<absl::string_view>()));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Verify decodeHeaders signs when use_unsigned_payload is true and end_stream is true.
TEST_F(AwsRequestSigningFilterTest, DecodeHeadersSignsUnsignedPayloadHeaderOnly) {
  setup();
  expectCredentials();

  filter_config_->use_unsigned_payload_ = true;
  EXPECT_CALL(*(filter_config_->signer_),
              signUnsignedPayload(An<Http::RequestHeaderMap&>(), An<Common::Aws::Credentials>(),
                                  An<absl::string_view>()));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

// Verify decodeHeaders does not sign when use_unsigned_payload is false and end_stream is false.
TEST_F(AwsRequestSigningFilterTest, DecodeHeadersStopsIterationWithoutSigning) {
  setup();
  expectCredentials();

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Verify decodeData does not sign when end_stream is false.
TEST_F(AwsRequestSigningFilterTest, DecodeDataStopsIterationWithoutSigning) {
  setup();
  expectCredentials();

  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));
}

// Verify decodeData signs when end_stream is true (empty payload).
TEST_F(AwsRequestSigningFilterTest, DecodeDataSignsEmptyPayloadAndContinues) {
  InSequence seq;
  setup();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // sha256('')
  const std::string hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  Buffer::OwnedImpl buffer;
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false));
  expectCredentials();

  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*(filter_config_->signer_),
              sign(HeaderMapEqualRef(&headers), An<Common::Aws::Credentials>(), hash,
                   An<absl::string_view>()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_added_.value());
  EXPECT_EQ(1UL, filter_config_->stats_.payload_signing_added_.value());
}

// Verify decodeData signs when end_stream is true (empty payload).
TEST_F(AwsRequestSigningFilterTest, DecodeDataSignsPayloadAndContinues) {
  InSequence seq;
  setup();
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // sha256('Action=SignThis')
  const std::string hash = "1db26ef86fca9f7c54d2273d4673a4f2a614fadf3185d16288d454619f1cf491";
  Buffer::OwnedImpl buffer("Action=SignThis");
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false));
  expectCredentials();
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*(filter_config_->signer_),
              sign(HeaderMapEqualRef(&headers), An<Common::Aws::Credentials>(), hash,
                   An<absl::string_view>()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Verify filter functionality when a host rewrite happens for header only request.
TEST_F(AwsRequestSigningFilterTest, SignWithHostRewrite) {
  setup();
  expectCredentials();
  filter_config_->host_rewrite_ = "foo";
  EXPECT_CALL(*(filter_config_->signer_),
              signEmptyPayload(An<Http::RequestHeaderMap&>(), An<Common::Aws::Credentials>(),
                               An<absl::string_view>()));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ("foo", headers.getHostValue());
  EXPECT_EQ(1UL, filter_config_->stats_.signing_added_.value());
}

// Verify filter functionality when signing fails in decodeHeaders.
TEST_F(AwsRequestSigningFilterTest, SignFails) {
  setup();
  expectCredentials();
  EXPECT_CALL(*(filter_config_->signer_),
              signEmptyPayload(An<Http::RequestHeaderMap&>(), An<Common::Aws::Credentials>(),
                               An<absl::string_view>()))
      .WillOnce(Invoke([](Http::HeaderMap&, const Common::Aws::Credentials,
                          const absl::string_view) -> absl::Status {
        return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
      }));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_failed_.value());
}

// Verify filter functionality when signing fails in decodeData.
TEST_F(AwsRequestSigningFilterTest, DecodeDataSignFails) {
  setup();
  expectCredentials();

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*(filter_config_->signer_),
              sign(An<Http::RequestHeaderMap&>(), An<Common::Aws::Credentials>(),
                   An<const std::string&>(), An<absl::string_view>()))
      .WillOnce(Invoke([](Http::HeaderMap&, const Common::Aws::Credentials, const std::string&,
                          const absl::string_view) -> absl::Status {
        return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
      }));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_failed_.value());
  EXPECT_EQ(1UL, filter_config_->stats_.payload_signing_failed_.value());
}

// Verify FilterConfigImpl getters.
TEST_F(AwsRequestSigningFilterTest, FilterConfigImplGetters) {
  Stats::IsolatedStoreImpl stats;
  auto signer = std::make_unique<Common::Aws::MockSigner>();
  auto credentials_provider = std::make_shared<Common::Aws::MockCredentialsProvider>();
  const auto* signer_ptr = signer.get();
  FilterConfigImpl config(std::move(signer), credentials_provider, "prefix", *stats.rootScope(),
                          "foo", true);

  EXPECT_EQ(signer_ptr, &config.signer());
  EXPECT_EQ(0UL, config.stats().signing_added_.value());
  EXPECT_EQ("foo", config.hostRewrite());
  EXPECT_EQ(true, config.useUnsignedPayload());
  EXPECT_EQ(credentials_provider, config.credentialsProvider());
}

// Verify filter functionality when a host rewrite happens on route-level config.
TEST_F(AwsRequestSigningFilterTest, PerRouteConfigSignWithHostRewrite) {
  setup();
  filter_config_->host_rewrite_ = "original-host";

  Stats::IsolatedStoreImpl stats;
  auto signer = std::make_unique<Common::Aws::MockSigner>();
  auto credentials_provider = std::make_shared<Common::Aws::MockCredentialsProvider>();
  EXPECT_CALL(*(credentials_provider), credentialsPending(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*(credentials_provider), getCredentials()).WillRepeatedly(Return(credentials_));

  EXPECT_CALL(*(signer), signEmptyPayload(An<Http::RequestHeaderMap&>(),
                                          An<Common::Aws::Credentials>(), An<absl::string_view>()));

  FilterConfigImpl per_route_config(std::move(signer), credentials_provider, "prefix",
                                    *stats.rootScope(), "overridden-host", false);
  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&per_route_config));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ("overridden-host", headers.getHostValue());
}

} // namespace
} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
