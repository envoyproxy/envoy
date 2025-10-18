#include <string>

#include "envoy/extensions/filters/http/transform/v3/transform.pb.h"

#include "source/extensions/filters/http/transform/transform.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {
namespace {

using testing::NiceMock;
using testing::Return;

class TransformTest : public ::testing::Test {
protected:
  TransformTest() = default;

  void initializeFilter(const std::string& yaml_config, const std::string& route_yaml_config) {
    envoy::extensions::filters::http::transform::v3::TransformConfig config;
    TestUtility::loadFromYaml(yaml_config, config);
    config_.reset();
    absl::Status status;
    config_ = std::make_shared<TransformConfig>(config, factory_context_, status);
    ASSERT_TRUE(status.ok()) << "TransformConfig creation failed: " << status.message();

    if (!route_yaml_config.empty()) {
      envoy::extensions::filters::http::transform::v3::TransformConfig route_config;
      TestUtility::loadFromYaml(route_yaml_config, route_config);
      route_config_ = std::make_shared<TransformConfig>(route_config, factory_context_, status);
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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

  std::shared_ptr<TransformConfig> config_;
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
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
  patch_format_string: true
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
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
  patch_format_string: true
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(TransformTest, TransformRequestNoBodyButHasTrailers) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(request_headers_.get_("content-length"), "");
}

TEST_F(TransformTest, TransformRequestNoRequestTransformConfigured) {
  const std::string yaml_config = R"EOF(
response_transform: {}
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl request_body(R"({"body-key": "body-value"})");
  const size_t body_length = request_body.length();
  request_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->decodeData(request_body, true), Http::FilterDataStatus::Continue);

  // Since no request transform is configured, no transformation should be applied.
  EXPECT_EQ(request_headers_.get_("content-length"), std::to_string(body_length));
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersAndClearRouteCache) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
  patch_format_string: true
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
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersAndClearClusterCache) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
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
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersAndNonBodyMergeMode) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
  patch_format_string: false
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
}

TEST_F(TransformTest, TransformRequestBodyAndHeadersWithTrailers) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
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
}

TEST_F(TransformTest, TransformRequestHeadersAndRouteOverride) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%REQ(header-key)%"
      new-body-key: "%RQ_BODY(body-key)%"
)EOF";

  // Only header mutation is used in the route config to override the filter level config.
  const std::string route_yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body-by-route"
        value: "%RQ_BODY(body-key)%"
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
}

TEST_F(TransformTest, TransformRequestBodyParsingError) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
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
  // The content-length should remain unchanged since the body could not be transformed.
  EXPECT_EQ(request_headers_.get_("content-length"), std::to_string(body_length));
}

TEST_F(TransformTest, TransformRequestPatchBodyParsingError) {
  const std::string yaml_config = R"EOF(
request_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RQ_BODY(body-key)%"
  body_format_string:
    text_format_source:
      inline_string: "%REQ(header-key)%"
  patch_format_string: true
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

  EXPECT_EQ(request_headers_.get_("x-new-header-from-body"), "body-value");
}

TEST_F(TransformTest, TransformResponseNonJsonBody) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%RESP(header-key)%"
      new-body-key: "%RS_BODY(body-key)%"
  patch_format_string: true
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
  EXPECT_EQ(response_headers_.get_("content-length"), std::to_string(body_length));
}

TEST_F(TransformTest, TransformResponseHeadersOnlyResponse) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%RESP(header-key)%"
      new-body-key: "%RS_BODY(body-key)%"
  patch_format_string: true
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(TransformTest, TransformResponseNoBodyButHasTrailers) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%RESP(header-key)%"
      new-body-key: "%RS_BODY(body-key)%"
)EOF";

  initializeFilter(yaml_config, "");

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(filter_->encodeTrailers(response_trailers_), Http::FilterTrailersStatus::Continue);

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "");
  // The content-length should be removed since the body has changed.
  EXPECT_EQ(response_headers_.get_("content-length"), "");
}

TEST_F(TransformTest, TransformResponseNoResponseTransformConfigured) {
  const std::string yaml_config = R"EOF(
request_transform: {}
)EOF";

  initializeFilter(yaml_config, "");
  Buffer::OwnedImpl response_body(R"({"body-key": "body-value"})");
  const size_t body_length = response_body.length();
  response_headers_.setContentLength(body_length);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeData(response_body, true), Http::FilterDataStatus::Continue);

  // Since no response transform is configured, no transformation should be applied.
  EXPECT_EQ(response_headers_.get_("content-length"), std::to_string(body_length));
}

TEST_F(TransformTest, TransformResponseBodyAndHeaders) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%RESP(header-key)%"
      new-body-key: "%RS_BODY(body-key)%"
    content_type: "application/json"
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
}

TEST_F(TransformTest, TransformResponseBodyAndHeadersNonBodyMergeMode) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%RESP(header-key)%"
      new-body-key: "%RS_BODY(body-key)%"
  patch_format_string: false
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
}

TEST_F(TransformTest, TransformResponseBodyAndHeadersWithTrailers) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    json_format:
      raw-key: "raw-value"
      header-key: "%RESP(header-key)%"
      new-body-key: "%RS_BODY(body-key)%"
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
}

TEST_F(TransformTest, TransformResponseBodyParsingError) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
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
}

TEST_F(TransformTest, TransformResponsePatchBodyParsingError) {
  const std::string yaml_config = R"EOF(
response_transform:
  headers_mutations:
  - append:
      header:
        key: "x-new-header-from-body"
        value: "%RS_BODY(body-key)%"
  body_format_string:
    text_format_source:
      inline_string: "%RESP(header-key)%"
  patch_format_string: true
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

  EXPECT_EQ(response_headers_.get_("x-new-header-from-body"), "body-value");
}

} // namespace
} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
