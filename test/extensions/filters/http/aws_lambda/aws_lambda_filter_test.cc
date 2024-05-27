#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"
#include "source/extensions/filters/http/aws_lambda/request_response.pb.validate.h"

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
using ::testing::An;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Pair;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::UnorderedElementsAre;

class FilterSettingsMock : public FilterSettings {
public:
  FilterSettingsMock(InvocationMode mode, bool payload_passthrough, const std::string& host_rewrite)
      : invocation_mode_(mode), payload_passthrough_(payload_passthrough),
        host_rewrite_(host_rewrite) {}

  const Arn& arn() const override { return arn_; }
  bool payloadPassthrough() const override { return payload_passthrough_; }
  InvocationMode invocationMode() const override { return invocation_mode_; }
  const std::string& hostRewrite() const override { return host_rewrite_; }
  Extensions::Common::Aws::Signer& signer() override { return *signer_; }

  Arn arn_{parseArn("arn:aws:lambda:us-west-2:1337:function:fun").value()};
  InvocationMode invocation_mode_;
  bool payload_passthrough_;
  const std::string host_rewrite_;
  std::shared_ptr<NiceMock<MockSigner>> signer_{std::make_shared<NiceMock<MockSigner>>()};
};

class AwsLambdaFilterTest : public ::testing::Test {
public:
  AwsLambdaFilterTest() = default;

  void setupFilter(const FilterSettingsSharedPtr& settings, bool upstream) {
    filter_ = std::make_unique<Filter>(settings, stats_, upstream);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  std::shared_ptr<FilterSettingsMock> setupDownstreamFilter(InvocationMode mode, bool passthrough,
                                                            const std::string& host_rewrite) {
    auto filter_settings = std::make_shared<FilterSettingsMock>(mode, passthrough, host_rewrite);
    setupFilter(filter_settings, false);
    setupClusterMetadata();
    return filter_settings;
  }

  void setupClusterMetadata() {
    ProtobufWkt::Struct cluster_metadata;
    TestUtility::loadFromYaml(metadata_yaml_, cluster_metadata);
    metadata_.mutable_filter_metadata()->insert({"com.amazonaws.lambda", cluster_metadata});
    ON_CALL(*decoder_callbacks_.cluster_info_, metadata()).WillByDefault(ReturnRef(metadata_));
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  envoy::config::core::v3::Metadata metadata_;
  Stats::IsolatedStoreImpl stats_store_;
  FilterStats stats_ = generateStats("test", *stats_store_.rootScope());
  const std::string metadata_yaml_ = "egress_gateway: true";
};

/**
 * Requests that are _not_ header only, should result in StopIteration.
 */
TEST_F(AwsLambdaFilterTest, DecodingHeaderStopIteration) {
  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * Signing failure in decodeHeaders passthrough
 */
TEST_F(AwsLambdaFilterTest, SigningFailureDecodeHeadersPassthrough) {
  auto filter_settings_ =
      setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  EXPECT_CALL(*(filter_settings_->signer_),
              signEmptyPayload(An<Http::RequestHeaderMap&>(), An<absl::string_view>()))
      .WillOnce(Invoke([](Http::HeaderMap&, const absl::string_view) -> absl::Status {
        return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
      }));

  Http::TestRequestHeaderMapImpl input_headers;
  const auto result = filter_->decodeHeaders(input_headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * Signing failure in decodeHeaders no passthrough
 */
TEST_F(AwsLambdaFilterTest, SigningFailureDecodeHeadersNoPassthrough) {
  auto filter_settings_ =
      setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  EXPECT_CALL(*(filter_settings_->signer_), sign(An<Http::RequestHeaderMap&>(),
                                                 An<const std::string&>(), An<absl::string_view>()))
      .WillOnce(Invoke([](Http::HeaderMap&, const std::string&,
                          const absl::string_view) -> absl::Status {
        return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
      }));
  Http::TestRequestHeaderMapImpl headers;
  headers.setHost("any_host");
  headers.setPath("/");
  headers.setMethod("POST");
  const auto result = filter_->decodeHeaders(headers, true);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * Signing failure in decodeData
 */
TEST_F(AwsLambdaFilterTest, SigningFailureDecodeData) {

  auto filter_settings =
      setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  Http::TestRequestHeaderMapImpl headers;
  filter_->decodeHeaders(headers, false /*end_stream*/);
  Buffer::OwnedImpl buffer;

  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*(filter_settings->signer_), sign(An<Http::RequestHeaderMap&>(),
                                                An<const std::string&>(), An<absl::string_view>()))
      .WillOnce(Invoke([](Http::HeaderMap&, const std::string&,
                          const absl::string_view) -> absl::Status {
        return absl::Status{absl::StatusCode::kInvalidArgument, "Message is missing :path header"};
      }));

  const auto data_result = filter_->decodeData(buffer, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, data_result);
}

/**
 * Header only pass-through requests should be signed and Continue iteration.
 */
TEST_F(AwsLambdaFilterTest, HeaderOnlyShouldContinue) {
  auto filter_settings =
      setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  EXPECT_CALL(*filter_settings->signer_,
              signEmptyPayload(An<Http::RequestHeaderMap&>(), An<absl::string_view>()));
  Http::TestRequestHeaderMapImpl input_headers;
  const auto result = filter_->decodeHeaders(input_headers, true /*end_stream*/);
  EXPECT_EQ("/2015-03-31/functions/arn:aws:lambda:us-west-2:1337:function:fun/invocations",
            input_headers.getPathValue());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);

  Http::TestResponseHeaderMapImpl response_headers;
  const auto encode_result = filter_->encodeHeaders(response_headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, encode_result);
}

/**
 * Cluster Metadata is not needed if the filter is loaded as upstream filter
 */
TEST_F(AwsLambdaFilterTest, ClusterMetadataIsNotNeededInUpstreamMode) {
  auto filter_settings =
      std::make_shared<FilterSettingsMock>(InvocationMode::Synchronous, true /*passthrough*/, "");
  setupFilter(filter_settings, true);

  EXPECT_CALL(*filter_settings->signer_,
              signEmptyPayload(An<Http::RequestHeaderMap&>(), An<absl::string_view>()));
  Http::TestRequestHeaderMapImpl input_headers;
  const auto result = filter_->decodeHeaders(input_headers, true /*end_stream*/);
  EXPECT_EQ("/2015-03-31/functions/arn:aws:lambda:us-west-2:1337:function:fun/invocations",
            input_headers.getPathValue());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);

  Http::TestResponseHeaderMapImpl response_headers;
  const auto encode_result = filter_->encodeHeaders(response_headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, encode_result);
}

/**
 * If there's a per route config and the target cluster has the _wrong_ metadata, then skip the
 * filter.
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigWrongClusterMetadata) {
  const std::string metadata_yaml = R"EOF(
  egress_gateway: true
  )EOF";

  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");

  ProtobufWkt::Struct cluster_metadata;
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(metadata_yaml, cluster_metadata);
  metadata.mutable_filter_metadata()->insert({"WrongMetadataKey", cluster_metadata});

  FilterSettingsMock route_settings{InvocationMode::Synchronous, true /*passthrough*/, ""};
  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&route_settings));

  ON_CALL(*decoder_callbacks_.cluster_info_, metadata()).WillByDefault(ReturnRef(metadata));
  Http::TestRequestHeaderMapImpl headers;

  const auto decode_header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, decode_header_result);

  Buffer::OwnedImpl buf;
  const auto decode_data_result = filter_->decodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, decode_data_result);
  EXPECT_EQ(0, buf.length());
}

/**
 * If there's a per route config and the target cluster has the _correct_ metadata, then we should
 * process the request (i.e. StopIteration if end_stream is false)
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigCorrectClusterMetadata) {
  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");

  FilterSettingsMock route_settings{InvocationMode::Synchronous, true /*passthrough*/, ""};
  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&route_settings));

  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * If there's a per route config which has lambda function arn from different region
 * then the SigV4 signer will sign with the region where the lambda function is present.
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigCorrectRegionForSigning) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  const absl::string_view override_region = "us-west-1";
  FilterSettingsMock route_settings{InvocationMode::Synchronous, true /*passthrough*/, ""};
  route_settings.arn_ =
      parseArn(fmt::format("arn:aws:lambda:{}:1337:function:fun", override_region)).value();
  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&route_settings));

  EXPECT_CALL(*route_settings.signer_,
              signEmptyPayload(An<Http::RequestHeaderMap&>(), override_region));
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(fmt::format("/2015-03-31/functions/arn:aws:lambda:{}:1337:function:fun/invocations",
                        override_region),
            headers.getPathValue());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataRecordsPayloadSize) {
  NiceMock<Stats::MockStore> store;
  NiceMock<Stats::MockHistogram> histogram;
  EXPECT_CALL(store, histogram(_, _)).WillOnce(ReturnRef(histogram));

  setupClusterMetadata();

  auto filter_settings =
      std::make_shared<FilterSettingsMock>(InvocationMode::Synchronous, true /*passthrough*/, "");
  filter_ =
      std::make_unique<Filter>(filter_settings, generateStats("test", *store.rootScope()), false);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  // Payload
  Buffer::OwnedImpl buffer;
  const std::string data(100, 'Z');
  buffer.add(data);

  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(histogram, recordValue(100));

  filter_->decodeData(buffer, true);
}

TEST_F(AwsLambdaFilterTest, DecodeDataShouldBuffer) {
  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  setupClusterMetadata();
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;
  const auto data_result = filter_->decodeData(buffer, false);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, data_result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataShouldSign) {
  auto filter_settings =
      setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;

  InSequence seq;
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*filter_settings->signer_, sign(An<Http::RequestHeaderMap&>(),
                                              An<const std::string&>(), An<absl::string_view>()));

  const auto data_result = filter_->decodeData(buffer, true /*end_stream*/);
  EXPECT_EQ("/2015-03-31/functions/arn:aws:lambda:us-west-2:1337:function:fun/invocations",
            headers.getPathValue());
  EXPECT_EQ(Http::FilterDataStatus::Continue, data_result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataSigningWithPerRouteConfig) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");

  const absl::string_view override_region = "us-west-1";
  FilterSettingsMock route_settings{InvocationMode::Synchronous, true /*passthrough*/, ""};
  route_settings.arn_ =
      parseArn(fmt::format("arn:aws:lambda:{}:1337:function:fun", override_region)).value();
  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&route_settings));

  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;

  InSequence seq;
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*route_settings.signer_,
              sign(An<Http::RequestHeaderMap&>(), An<const std::string&>(), override_region));

  const auto data_result = filter_->decodeData(buffer, true /*end_stream*/);
  EXPECT_EQ(fmt::format("/2015-03-31/functions/arn:aws:lambda:{}:1337:function:fun/invocations",
                        override_region),
            headers.getPathValue());
  EXPECT_EQ(Http::FilterDataStatus::Continue, data_result);
}

TEST_F(AwsLambdaFilterTest, DecodeHeadersInvocationModeSetsHeader) {
  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  setupClusterMetadata();

  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, header_result);

  std::string invocation_header_value;
  headers.iterate([&invocation_header_value](const Http::HeaderEntry& entry) {
    if (entry.key().getStringView() == "x-amz-invocation-type") {
      invocation_header_value.append(std::string(entry.value().getStringView()));
      return Http::HeaderMap::Iterate::Break;
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  EXPECT_EQ("RequestResponse", invocation_header_value);
}

/**
 * A header-only request with pass-through turned off should result in:
 * - a request with JSON body.
 * - content-length header set appropriately
 * - content-type header set to application/json
 * - headers with multiple values coalesced with a comma
 */
TEST_F(AwsLambdaFilterTest, DecodeHeadersOnlyRequestWithJsonOn) {
  using source::extensions::filters::http::aws_lambda::Request;
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");

  Buffer::OwnedImpl json_buf;
  auto on_add_decoded_data = [&json_buf](Buffer::Instance& buf, bool) { json_buf.move(buf); };
  ON_CALL(decoder_callbacks_, addDecodedData(_, _)).WillByDefault(Invoke(on_add_decoded_data));
  Http::TestRequestHeaderMapImpl headers;
  headers.setContentLength(0);
  headers.setPath("/resource?proxy=envoy");
  headers.setMethod("GET");
  headers.addCopy("x-custom-header", "unit");
  headers.addCopy("x-custom-header", "test");
  const auto header_result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ("/2015-03-31/functions/arn:aws:lambda:us-west-2:1337:function:fun/invocations",
            headers.getPathValue());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, header_result);

  // Assert it's not empty
  ASSERT_GT(json_buf.length(), 0);

  ASSERT_NE(headers.ContentType(), nullptr);
  EXPECT_EQ("application/json", headers.getContentTypeValue());

  // Assert the true (post-transformation) content-length sent to the Lambda endpoint.
  ASSERT_NE(headers.ContentLength(), nullptr);
  EXPECT_EQ(fmt::format("{}", json_buf.length()), headers.getContentLengthValue());

  // The best way to verify the generated JSON is to deserialize it and inspect it.
  Request req;
  TestUtility::loadFromJson(json_buf.toString(), req);

  // Assert the content-length wrapped in JSON reflects the original request's value.
  EXPECT_THAT(req.headers(), UnorderedElementsAre(Pair("content-length", "0"),
                                                  Pair("x-custom-header", "unit,test")));
  EXPECT_THAT(req.query_string_parameters(), UnorderedElementsAre(Pair("proxy", "envoy")));
  EXPECT_STREQ("/resource?proxy=envoy", req.raw_path().c_str());
  EXPECT_FALSE(req.is_base64_encoded());
  EXPECT_TRUE(req.body().empty());
  EXPECT_STREQ("GET", req.method().c_str());
}

/**
 * A request with text payload and pass-through turned off should result in:
 * - a request with JSON body containing the original payload
 * - content-length header set appropriately
 * - content-type header set to application/json
 * - headers with multiple values coalesced with a comma
 */
TEST_F(AwsLambdaFilterTest, DecodeDataWithTextualBodyWithJsonOn) {
  using source::extensions::filters::http::aws_lambda::Request;
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");

  Buffer::OwnedImpl decoded_buf;
  constexpr absl::string_view expected_plain_text = "Foo bar bazz";
  decoded_buf.add(expected_plain_text);

  auto on_modify_decoding_buffer = [&decoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(decoded_buf);
  };
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillRepeatedly(Return(&decoded_buf));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer)
      .WillRepeatedly(Invoke(on_modify_decoding_buffer));

  std::array<const char*, 4> textual_mime_types = {"application/json", "application/javascript",
                                                   "application/xml", "text/plain"};

  for (auto mime_type : textual_mime_types) {
    Http::TestRequestHeaderMapImpl headers;
    headers.setContentLength(expected_plain_text.length());
    headers.setPath("/resource?proxy=envoy");
    headers.setMethod("POST");
    headers.setContentType(mime_type);
    headers.addCopy("x-custom-header", "unit");
    headers.addCopy("x-custom-header", "test");
    const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
    ASSERT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);

    const auto data_result = filter_->decodeData(decoded_buf, true /*end_stream*/);
    ASSERT_EQ(Http::FilterDataStatus::Continue, data_result);

    // Assert decoded buffer is not drained
    ASSERT_GT(decoded_buf.length(), 0);

    ASSERT_NE(headers.ContentType(), nullptr);
    EXPECT_EQ("application/json", headers.getContentTypeValue());

    // Assert the true (post-transformation) content-length sent to the Lambda endpoint.
    ASSERT_NE(headers.ContentLength(), nullptr);
    EXPECT_EQ(fmt::format("{}", decoded_buf.length()), headers.getContentLengthValue());

    // The best way to verify the generated JSON is to deserialize it and inspect it.
    Request req;
    TestUtility::loadFromJson(decoded_buf.toString(), req);

    // Assert the content-length wrapped in JSON reflects the original request's value.
    EXPECT_THAT(req.headers(),
                UnorderedElementsAre(
                    Pair("content-length", fmt::format("{}", expected_plain_text.length())),
                    Pair("content-type", mime_type), Pair("x-custom-header", "unit,test")));
    EXPECT_THAT(req.query_string_parameters(), UnorderedElementsAre(Pair("proxy", "envoy")));
    EXPECT_STREQ("/resource?proxy=envoy", req.raw_path().c_str());
    EXPECT_STREQ("POST", req.method().c_str());
    EXPECT_FALSE(req.is_base64_encoded());
    ASSERT_FALSE(req.body().empty());
    EXPECT_STREQ(expected_plain_text.data(), req.body().c_str());

    // reset the buffer for the next iteration
    decoded_buf.drain(decoded_buf.length());
    decoded_buf.add(expected_plain_text);
  }
}

/**
 * A request with binary payload and pass-through turned off should result in a JSON payload with
 * isBase64Encoded flag set.
 * binary payload is determined by looking at both transfer-encoding and content-type.
 */
TEST_F(AwsLambdaFilterTest, DecodeDataWithBinaryBodyWithJsonOn) {
  using source::extensions::filters::http::aws_lambda::Request;
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");

  Buffer::OwnedImpl decoded_buf;
  const absl::string_view fake_binary_data = "this should get base64 encoded";
  decoded_buf.add(fake_binary_data);
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillRepeatedly(Return(&decoded_buf));
  auto on_modify_decoding_buffer = [&decoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(decoded_buf);
  };
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer)
      .WillRepeatedly(Invoke(on_modify_decoding_buffer));
  std::array<absl::string_view, 3> binary_mime_types = {"", "application/pdf", "gzipped"};
  for (auto mime_type : binary_mime_types) {
    Http::TestRequestHeaderMapImpl headers;
    headers.setPath("/");
    headers.setMethod("POST");
    headers.setContentLength(fake_binary_data.length());
    if (mime_type == "gzipped") {
      headers.setTransferEncoding("gzip");
    } else if (!mime_type.empty()) {
      headers.setContentType(mime_type);
    }
    const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
    ASSERT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);

    const auto data_result = filter_->decodeData(decoded_buf, true /*end_stream*/);
    ASSERT_EQ(Http::FilterDataStatus::Continue, data_result);

    // The best way to verify the generated JSON is to deserialize it and inspect it.
    Request req;
    TestUtility::loadFromJson(decoded_buf.toString(), req);

    ASSERT_TRUE(req.is_base64_encoded());
    ASSERT_FALSE(req.body().empty());
    ASSERT_STREQ(req.body().c_str(), "dGhpcyBzaG91bGQgZ2V0IGJhc2U2NCBlbmNvZGVk");

    // reset the buffer for the next iteration
    decoded_buf.drain(decoded_buf.length());
    decoded_buf.add(fake_binary_data);
  }
}

TEST_F(AwsLambdaFilterTest, EncodeHeadersEndStreamShouldSkip) {
  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");

  Http::TestResponseHeaderMapImpl headers;
  auto result = filter_->encodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);

  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");

  result = filter_->encodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If the Lambda function itself raises an error (syntax, exception, etc.) then we should skip
 * encoding headers and skip the filter.
 */
TEST_F(AwsLambdaFilterTest, EncodeHeadersWithLambdaErrorShouldSkipAndContinue) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  setupClusterMetadata();

  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  headers.addCopy(Http::LowerCaseString("x-Amz-Function-Error"), "unhandled");
  auto result = filter_->encodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If Lambda returns a 5xx error then we should skip encoding headers and skip the filter.
 */
TEST_F(AwsLambdaFilterTest, EncodeHeadersWithLambda5xxShouldSkipAndContinue) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(500);
  auto result = filter_->encodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * encodeHeaders() in a happy path should stop iteration.
 */
TEST_F(AwsLambdaFilterTest, EncodeHeadersStopsIteration) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  auto result = filter_->encodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * encodeData() data in pass-through mode should simply return Continue.
 * This is true whether end_stream is true or false.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataInPassThroughMode) {
  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  Buffer::OwnedImpl buf;
  auto result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  result = filter_->encodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  setupDownstreamFilter(InvocationMode::Synchronous, true /*passthrough*/, "");
  result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  result = filter_->encodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
}

/**
 * encodeData() data in asynchronous mode should simply return Continue.
 * This is true whether end_stream is true or false.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataInAsynchrnous) {
  setupDownstreamFilter(InvocationMode::Asynchronous, false /*passthrough*/, "");
  Buffer::OwnedImpl buf;
  auto result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  result = filter_->encodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
}

/**
 * encodeData() data in JSON mode should stop iteration if end_stream is false.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeStopIterationAndBuffer) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Buffer::OwnedImpl buf;
  auto result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, result);
}

TEST_F(AwsLambdaFilterTest, EncodeDataAddsLastChunk) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  Buffer::OwnedImpl buf(std::string("foobar"));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&buf));
  filter_->encodeData(buf, true /*end_stream*/);
}

/**
 * encodeData() data in JSON mode without a 'body' key should translate the 'headers' key to HTTP
 * headers while ignoring any HTTP/2 pseudo-headers.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeTransformToHttp) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_response = R"EOF(
  {
      "statusCode": 201,
      "headers": {
                    "x-awesome-header": "awesome value",
                    ":other": "should_never_make_it"
                 },
      "cookies": ["session-id=42; Secure; HttpOnly", "user=joe"]
  }
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_response);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  ASSERT_NE(nullptr, headers.Status());
  EXPECT_EQ("201", headers.getStatusValue());

  EXPECT_FALSE(headers.has(":other"));
  EXPECT_EQ("awesome value", headers.get_("x-awesome-header"));
  EXPECT_EQ("application/json", headers.get_("content-type"));

  std::vector<std::string> cookies;
  headers.iterate([&cookies](const Http::HeaderEntry& entry) {
    if (entry.key().getStringView() == Http::Headers::get().SetCookie.get()) {
      cookies.emplace_back(entry.value().getStringView());
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  EXPECT_THAT(cookies, ElementsAre("session-id=42; Secure; HttpOnly", "user=joe"));
}

/**
 * encodeData() data in JSON mode should respect content-type header.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeContentTypeHeader) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_response = R"EOF(
  {
      "statusCode": 201,
      "headers": {"content-type": "text/plain"}
  }
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_response);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  ASSERT_NE(nullptr, headers.Status());
  EXPECT_EQ("201", headers.getStatusValue());
  EXPECT_EQ("text/plain", headers.get_("content-type"));
}

/**
 * encodeData() in JSON mode with a non-empty body should translate the body to plain text if it was
 * base64-encoded.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeBase64EncodedBody) {
  auto filter_settings =
      setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_base64_body = R"EOF(
  {
      "statusCode": 201,
      "body": "Q29mZmVl",
      "isBase64Encoded": true
  }
  )EOF";

  constexpr auto json_plain_text_body = R"EOF(
  {
      "statusCode": 201,
      "body": "Beans",
      "isBase64Encoded": false
  }
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_base64_body);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
  EXPECT_STREQ("Coffee", encoded_buf.toString().c_str());

  encoded_buf.drain(encoded_buf.length());

  encoded_buf.add(json_plain_text_body);
  result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
  EXPECT_STREQ("Beans", encoded_buf.toString().c_str());

  EXPECT_EQ(0ul, filter_->stats().server_error_.value());
}

/**
 * Encode data in JSON mode _returning_ invalid JSON payload should result in a 500 error.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeInvalidJson) {
  auto filter_settings =
      setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "");
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_response = R"EOF(
  <response>
        <body>Does XML work??</body>
  </response>
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_response);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
  EXPECT_EQ(0, encoded_buf.length());

  ASSERT_NE(nullptr, headers.Status());
  EXPECT_EQ("500", headers.getStatusValue());

  EXPECT_EQ(1ul, filter_->stats().server_error_.value());
}

TEST_F(AwsLambdaFilterTest, SignWithHostRewrite) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "new_host");
  Http::TestRequestHeaderMapImpl headers;
  headers.setHost("any_host");
  headers.setPath("/");
  headers.setMethod("POST");
  const auto result = filter_->decodeHeaders(headers, true);

  EXPECT_EQ("new_host", headers.get_(":authority"));
  EXPECT_EQ("new_host", headers.Host()->value().getStringView());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

TEST_F(AwsLambdaFilterTest, SignWithHostRewritePerRoute) {
  setupDownstreamFilter(InvocationMode::Synchronous, false /*passthrough*/, "new_host");

  FilterSettingsMock route_settings{InvocationMode::Synchronous, false /*passthrough*/,
                                    "new_host_per_route"};
  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&route_settings));

  Http::TestRequestHeaderMapImpl headers;
  headers.setHost("any_host");
  headers.setPath("/");
  headers.setMethod("POST");
  const auto result = filter_->decodeHeaders(headers, true);

  EXPECT_EQ("new_host_per_route", headers.get_(":authority"));
  EXPECT_EQ("new_host_per_route", headers.Host()->value().getStringView());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
