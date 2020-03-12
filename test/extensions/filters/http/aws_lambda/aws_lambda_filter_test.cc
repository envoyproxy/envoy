#include "envoy/config/core/v3/base.pb.h"

#include "extensions/filters/http/aws_lambda/aws_lambda_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

namespace {

using Common::Aws::MockSigner;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;

constexpr auto Arn = "arn:aws:lambda:us-west-2:1337:function:fun";
class AwsLambdaFilterTest : public ::testing::Test {
public:
  void setupFilter(const FilterSettings& settings) {
    signer_ = std::make_shared<NiceMock<MockSigner>>();
    filter_ = std::make_unique<Filter>(settings, signer_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  std::shared_ptr<NiceMock<MockSigner>> signer_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

/**
 * Requests that are _not_ header only, should result in StopIteration.
 */
TEST_F(AwsLambdaFilterTest, DecodingHeaderStopIteration) {
  setupFilter({Arn, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * Header only requests should be signed and Continue iteration.
 * Also, if x-forwarded-proto header is found, it should be removed when signing.
 */
TEST_F(AwsLambdaFilterTest, HeaderOnlyShouldContinue) {
  setupFilter({Arn, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl input_headers{{":method", "GET"}, {"x-forwarded-proto", "http"}};
  const auto result = filter_->decodeHeaders(input_headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If there's a per-route configuration and the target cluster does not have the AWS Lambda
 * metadata, then we should skip the filter.
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigNoClusterMetadata) {
  setupFilter({Arn, true /*passthrough*/});
  FilterSettings route_settings{Arn, true /*passthrough*/};
  ON_CALL(decoder_callbacks_.route_->route_entry_,
          perFilterConfig(HttpFilterNames::get().AwsLambda))
      .WillByDefault(Return(&route_settings));
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If there's a per route config and the target cluster has the _wrong_ metadata, then skip the
 * filter.
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigWrongClusterMetadata) {
  const std::string metadata_yaml = R"EOF(
  egress_gateway: true
  )EOF";

  ProtobufWkt::Struct cluster_metadata;
  envoy::config::core::v3::Metadata metadata; // What should this type be?
  TestUtility::loadFromYaml(metadata_yaml, cluster_metadata);
  metadata.mutable_filter_metadata()->insert({"WrongMetadataKey", cluster_metadata});

  setupFilter({Arn, true /*passthrough*/});
  FilterSettings route_settings{Arn, true /*passthrough*/};
  ON_CALL(decoder_callbacks_.route_->route_entry_,
          perFilterConfig(HttpFilterNames::get().AwsLambda))
      .WillByDefault(Return(&route_settings));

  ON_CALL(*decoder_callbacks_.cluster_info_, metadata()).WillByDefault(ReturnRef(metadata));
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If there's a per route config and the target cluster has the _correct_ metadata, then we should
 * process the request (i.e. StopIteration if end_stream is false)
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigCorrectClusterMetadata) {
  const std::string metadata_yaml = R"EOF(
  egress_gateway: true
  )EOF";

  ProtobufWkt::Struct cluster_metadata;
  envoy::config::core::v3::Metadata metadata; // What should this type be?
  TestUtility::loadFromYaml(metadata_yaml, cluster_metadata);
  metadata.mutable_filter_metadata()->insert({"com.amazonaws.lambda", cluster_metadata});

  setupFilter({Arn, true /*passthrough*/});
  FilterSettings route_settings{Arn, true /*passthrough*/};
  ON_CALL(decoder_callbacks_.route_->route_entry_,
          perFilterConfig(HttpFilterNames::get().AwsLambda))
      .WillByDefault(Return(&route_settings));

  ON_CALL(*decoder_callbacks_.cluster_info_, metadata()).WillByDefault(ReturnRef(metadata));
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataShouldBuffer) {
  setupFilter({Arn, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;
  const auto data_result = filter_->decodeData(buffer, false);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, data_result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataShouldSign) {
  setupFilter({Arn, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;
  ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(&buffer));
  const auto data_result = filter_->decodeData(buffer, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, data_result);
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
