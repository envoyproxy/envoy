#include <string>

#include "envoy/extensions/filters/http/transform/v3/transform.pb.h"

#include "source/extensions/filters/http/transform/transform.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {
namespace {

using testing::NiceMock;
using testing::Return;

TEST(BodyFormatterProviderTest, BodyFormatterProviderTest) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  BodyFormatterProvider request_provider("body-key:sub-key", true /* request_body */);
  BodyFormatterProvider response_provider("body-key:sub-key", false /* request_body */);

  {
    // No BodyContextExtension present.
    Formatter::Context context;
    const auto value = request_provider.format(context, stream_info);
    EXPECT_FALSE(value.has_value());
    const auto proto_value = request_provider.formatValue(context, stream_info);
    EXPECT_EQ(proto_value.kind_case(), Protobuf::Value::KIND_NOT_SET);
  }

  {
    // BodyContextExtension present but no such key.
    Formatter::Context context;
    BodyContextExtension extension;
    context.setExtension(extension);

    const auto value = request_provider.format(context, stream_info);
    EXPECT_FALSE(value.has_value());
    const auto proto_value = request_provider.formatValue(context, stream_info);
    EXPECT_EQ(proto_value.kind_case(), Protobuf::Value::KIND_NOT_SET);
  }

  {
    // BodyContextExtension present with single string value.
    Formatter::Context context;
    BodyContextExtension extension;
    (*(*extension.response_body.mutable_fields())["body-key"]
          .mutable_struct_value()
          ->mutable_fields())["sub-key"]
        .set_string_value("response-body-value");
    context.setExtension(extension);

    const auto value = response_provider.format(context, stream_info);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "response-body-value");
    const auto proto_value = response_provider.formatValue(context, stream_info);
    EXPECT_EQ(proto_value.kind_case(), Protobuf::Value::kStringValue);
    EXPECT_EQ(proto_value.string_value(), "response-body-value");
  }

  {
    // BodyContextExtension present with non-string value.
    Formatter::Context context;
    BodyContextExtension extension;
    (*(*extension.request_body.mutable_fields())["body-key"]
          .mutable_struct_value()
          ->mutable_fields())["sub-key"]
        .set_number_value(1);
    context.setExtension(extension);

    const auto value = request_provider.format(context, stream_info);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "1");
    const auto proto_value = request_provider.formatValue(context, stream_info);
    EXPECT_EQ(proto_value.kind_case(), Protobuf::Value::kNumberValue);
    EXPECT_EQ(proto_value.number_value(), 1);
  }

  {
    // Get content from response.
    Formatter::Context context;
    BodyContextExtension extension;
    (*(*extension.response_body.mutable_fields())["body-key"]
          .mutable_struct_value()
          ->mutable_fields())["sub-key"]
        .set_bool_value(true);
    context.setExtension(extension);

    const auto value = response_provider.format(context, stream_info);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "true");
    const auto proto_value = response_provider.formatValue(context, stream_info);
    EXPECT_EQ(proto_value.kind_case(), Protobuf::Value::kBoolValue);
    EXPECT_EQ(proto_value.bool_value(), true);
  }
}

class TransformTest : public ::testing::Test {
protected:
  TransformTest() = default;

  void initializeFilter(const std::string& yaml_config, const std::string& route_yaml_config) {
    envoy::extensions::filters::http::transform::v3::TransformConfig config;
    TestUtility::loadFromYaml(yaml_config, config);
    config_.reset();
    absl::Status status;
    config_ = std::make_shared<FilterConfig>(config, "test", factory_context_, status);
    ASSERT_TRUE(status.ok()) << "Filter config creation failed: " << status.message();

    if (!route_yaml_config.empty()) {
      envoy::extensions::filters::http::transform::v3::TransformConfig route_config;
      TestUtility::loadFromYaml(route_yaml_config, route_config);
      route_config_ = std::make_shared<TransformConfig>(
          route_config, factory_context_.server_factory_context_, status);
      ASSERT_TRUE(status.ok()) << "TransformConfig of route creation failed: " << status.message();

      ON_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
          .WillByDefault(Return(route_config_.get()));
      ON_CALL(encoder_callbacks_, mostSpecificPerFilterConfig())
          .WillByDefault(Return(route_config_.get()));
    }

    filter_ = std::make_shared<TransformFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);

    ON_CALL(decoder_callbacks_, requestHeaders())
        .WillByDefault(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    ON_CALL(encoder_callbacks_, responseHeaders())
        .WillByDefault(Return(Http::ResponseHeaderMapOptRef(response_headers_)));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<TransformConfig> route_config_;
  std::shared_ptr<TransformFilter> filter_;
  Http::TestRequestHeaderMapImpl request_headers_{{":method", "POST"},
                                                  {":path", "/test"},
                                                  {"content-type", "application/json"},
                                                  {"header-key", "header-value"}};
  Http::TestResponseHeaderMapImpl response_headers_{
      {":status", "200"}, {"header-key", "header-value"}, {"content-type", "application/json"}};

  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

TEST_F(TransformTest, TransformRequestNonJsonBody) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body("This is not a JSON body");
  const size_t body_length = request_body.length();
  request_headers_.setContentType("plain/text");
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->decodeData(request_body, false), Http::FilterDataStatus::Continue);
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), Http::FilterTrailersStatus::Continue);

  // Since the body is not JSON, no transformation should be applied.
  EXPECT_EQ(request_headers_.get_("content-length"), std::to_string(body_length));
}

TEST_F(TransformTest, TransformRequestHeadersOnlyRequest) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformRequestNoBodyButHasTrailers) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "");

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformRequestNoRequestTransformConfigured) {
  const std::string yaml_config = R"EOF(
response_transformation: {}
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body(R"({"body-key": "body-value"})");
  const size_t body_length = request_body.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->decodeData(request_body, true), Http::FilterDataStatus::Continue);

  // Since no request transform is configured, no transformation should be applied.
  EXPECT_EQ(request_headers_.get_("content-length"), std::to_string(body_length));

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersAndClearRouteCache) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: MERGE
clear_route_cache: true
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, addDecodedData(testing::_, true));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);
        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
        {
            "raw-key": "raw-value",
            "header-key": "header-value",
            "new-body-key": "body-value",
            "body-key": "body-value"
        }
        )EOF"));
      });
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(filter_->decodeData(request_body_2, true), Http::FilterDataStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(request_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersAndClearClusterCache) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: MERGE
clear_cluster_cache: true
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, addDecodedData(testing::_, true));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);
        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
        {
            "raw-key": "raw-value",
            "header-key": "header-value",
            "new-body-key": "body-value",
            "body-key": "body-value"
        }
        )EOF"));
      });
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, refreshRouteCluster());
  EXPECT_EQ(filter_->decodeData(request_body_2, true), Http::FilterDataStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(request_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersAndNonBodyMergeMode) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: REPLACE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, addDecodedData(testing::_, true));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);
        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
        {
            "raw-key": "raw-value",
            "header-key": "header-value",
            "new-body-key": "body-value",
        }
        )EOF"));
      });
  EXPECT_EQ(filter_->decodeData(request_body_2, true), Http::FilterDataStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(request_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersWithTrailers) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);
  EXPECT_EQ(filter_->decodeData(request_body_2, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);
        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
        {
            "raw-key": "raw-value",
            "header-key": "header-value",
            "new-body-key": "body-value",
            "body-key": "body-value"
        }
        )EOF"));
      });
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(request_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformRequestHeadersAndRouteOverride) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%REQ(header-key)%"
        new-body-key: "%REQUEST_BODY(body-key)%"
    action: MERGE
)EOF";

  // Only header mutation is used in the route config to override the filter level config.
  const std::string route_yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body-by-route"
        value: "%REQUEST_BODY(body-key)%"
)EOF";

  initializeFilter(yaml_config, route_yaml_config);
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);
  EXPECT_EQ(filter_->decodeData(request_body_2, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(testing::_)).Times(0);

  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body-by-route"), "body-value");
  // The configuration of filter level should be overridden by route level and the body
  // transformation should not be skipped.
  EXPECT_EQ(request_headers_.get_("content-length"), std::to_string(body_length));

  EXPECT_EQ(config_->stats().rq_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformRequestBodyParsingError) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"(This is not a JSON body)");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, addDecodedData(testing::_, true));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_EQ(filter_->decodeData(request_body_2, true), Http::FilterDataStatus::Continue);

  // Since the body could not be parsed, no transformation should be applied.
  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "");
  EXPECT_EQ(config_->stats().rq_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformRequestPatchBodyParsingError) {
  const std::string yaml_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%REQUEST_BODY(body-key)%"
  body_transformation:
    body_format:
      text_format_source:
        inline_string: "%REQ(header-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body_1;
  Buffer::OwnedImpl request_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = request_body_2.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(decoder_callbacks_, addDecodedData(testing::_, true));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&request_body_2));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(testing::_)).Times(0);
  EXPECT_EQ(filter_->decodeData(request_body_2, true), Http::FilterDataStatus::Continue);

  // Since the patch body could not be applied, no transformation should be applied.
  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "");
  EXPECT_EQ(config_->stats().rq_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponseNonJsonBody) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body("This is not a JSON body");
  const size_t body_length = response_body.length();
  response_headers_.setContentType("plain/text");
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeData(response_body, false), Http::FilterDataStatus::Continue);
  EXPECT_EQ(filter_->encodeTrailers(response_trailers_), Http::FilterTrailersStatus::Continue);

  // Since the body is not JSON, no transformation should be applied.
  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponseHeadersOnlyResponse) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponseNoBodyButHasTrailers) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(filter_->encodeTrailers(response_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "");
  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponseNoResponseTransformConfigured) {
  const std::string yaml_config = R"EOF(
request_transformation: {}
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body(R"({"body-key": "body-value"})");
  const size_t body_length = response_body.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeData(response_body, true), Http::FilterDataStatus::Continue);

  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponseBodyAndHeaders) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body_1;
  Buffer::OwnedImpl response_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = response_body_2.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->encodeData(response_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&response_body_2));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);

        std::cout << "Transformed body: " << actual_body.toString() << std::endl;

        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
            {
                "raw-key": "raw-value",
                "header-key": "header-value",
                "new-body-key": "body-value",
                "body-key": "body-value"
            }
            )EOF"));
      });
  EXPECT_EQ(filter_->encodeData(response_body_2, true), Http::FilterDataStatus::Continue);

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(response_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rs_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformResponseBodyAndHeadersNonBodyMergeMode) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: REPLACE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body_1;
  Buffer::OwnedImpl response_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = response_body_2.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->encodeData(response_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&response_body_2));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);
        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
            {
                "raw-key": "raw-value",
                "header-key": "header-value",
                "new-body-key": "body-value",
            }
            )EOF"));
      });
  EXPECT_EQ(filter_->encodeData(response_body_2, true), Http::FilterDataStatus::Continue);

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(response_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rs_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformResponseBodyAndHeadersWithTrailers) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body_1;
  Buffer::OwnedImpl response_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = response_body_2.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->encodeData(response_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);
  EXPECT_EQ(filter_->encodeData(response_body_2, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&response_body_2));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(testing::_))
      .WillOnce([](std::function<void(Buffer::Instance&)> callback) {
        Buffer::OwnedImpl actual_body;
        callback(actual_body);
        EXPECT_TRUE(TestUtility::jsonStringEqual(actual_body.toString(),
                                                 R"EOF(
            {
                "raw-key": "raw-value",
                "header-key": "header-value",
                "new-body-key": "body-value",
                "body-key": "body-value"
            }
            )EOF"));
      });
  EXPECT_EQ(filter_->encodeTrailers(response_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "body-value");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(response_headers_.get_("content-length"), "");

  EXPECT_EQ(config_->stats().rs_transformed_.value(), 1);
}

TEST_F(TransformTest, TransformResponseBodyParsingError) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body_1;
  Buffer::OwnedImpl response_body_2(R"(This is not a JSON body)");
  const size_t body_length = response_body_2.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->encodeData(response_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(encoder_callbacks_, addEncodedData(testing::_, true));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&response_body_2));
  EXPECT_EQ(filter_->encodeData(response_body_2, true), Http::FilterDataStatus::Continue);

  // Since the body could not be parsed, no transformation should be applied.
  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "");
  // The content-length should remain unchanged since the body could not be transformed.
  EXPECT_EQ(response_headers_.get_("content-length"), std::to_string(body_length));

  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponsePatchBodyParsingError) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      text_format_source:
        inline_string: "%RESP(header-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body_1;
  Buffer::OwnedImpl response_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = response_body_2.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->encodeData(response_body_1, false),
            Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(encoder_callbacks_, addEncodedData(testing::_, true));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&response_body_2));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(testing::_)).Times(0);
  EXPECT_EQ(filter_->encodeData(response_body_2, true), Http::FilterDataStatus::Continue);

  // Since the patch body could not be applied, no transformation should be applied.
  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "");
  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

TEST_F(TransformTest, TransformResponseSkipForLocalReply) {
  const std::string yaml_config = R"EOF(
response_transformation:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RESPONSE_BODY(body-key)%"
  body_transformation:
    body_format:
      json_format:
        raw-key: "raw-value"
        header-key: "%RESP(header-key)%"
        new-body-key: "%RESPONSE_BODY(body-key)%"
    action: MERGE
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body_1;
  Buffer::OwnedImpl response_body_2(R"({"body-key": "body-value"})");
  const size_t body_length = response_body_2.length();
  response_headers_.setContentLength(body_length);

  Http::StreamFilterBase::LocalReplyData local_reply_data;
  filter_->onLocalReply(local_reply_data);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeData(response_body_1, false), Http::FilterDataStatus::Continue);
  EXPECT_EQ(filter_->encodeData(response_body_2, false), Http::FilterDataStatus::Continue);
  EXPECT_EQ(filter_->encodeTrailers(response_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "");
  EXPECT_EQ(config_->stats().rs_transformed_.value(), 0);
}

} // namespace
} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
