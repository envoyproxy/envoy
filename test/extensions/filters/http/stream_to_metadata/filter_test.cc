#include "source/common/config/metadata.h"
#include "source/extensions/filters/http/stream_to_metadata/config.h"
#include "source/extensions/filters/http/stream_to_metadata/filter.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

class StreamToMetadataFilterTest : public testing::Test {
public:
  StreamToMetadataFilterTest()
      : stream_info_(time_source_, nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  void SetUp() override { setupFilter(basic_config_); }

  void setupFilter(const std::string& yaml) {
    envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
    filter_ = std::make_unique<Filter>(config_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);

    ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  }

  uint64_t findCounter(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_store_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  void addEncodeDataChunks(const std::string& data, bool end_stream = false) {
    response_data_.add(data);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data_, end_stream));
    response_data_.drain(response_data_.length());
  }

  Protobuf::Value getMetadata(const std::string& ns, const std::string& key) {
    return Envoy::Config::Metadata::metadataValue(
        &encoder_callbacks_.streamInfo().dynamicMetadata(), ns, key);
  }

  const std::string basic_config_ = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
  )EOF";

  const std::string multi_namespace_config_ = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
          - metadata_namespace: "envoy.audit"
            key: "token_count"
            type: NUMBER
  )EOF";

  const std::string preserve_config_ = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
            preserve_existing_metadata_value: true
  )EOF";

  const std::string multi_rule_config_ = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
      - selector:
          json_path:
            path: ["model"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "model_name"
            type: STRING
        stop_processing_on_match: false
  )EOF";

  const std::string no_stop_config_ = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
        stop_processing_on_match: false
  )EOF";

  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  std::shared_ptr<FilterConfig> config_;
  std::unique_ptr<Filter> filter_;

  Http::TestResponseHeaderMapImpl response_headers_{{"content-type", "text/event-stream"}};
  Http::TestResponseHeaderMapImpl response_headers_with_params_{
      {"content-type", "text/event-stream; charset=utf-8"}};
  Buffer::OwnedImpl response_data_;

  static constexpr absl::string_view delimiter_ = "\n\n";
};

TEST_F(StreamToMetadataFilterTest, BadContentType) {
  Http::TestResponseHeaderMapImpl bad_headers{{"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(bad_headers, false));

  addEncodeDataChunks("data: {\"test\": \"value\"}\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 1);
}

TEST_F(StreamToMetadataFilterTest, NoContentTypeHeader) {
  Http::TestResponseHeaderMapImpl no_ct_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(no_ct_headers, false));

  addEncodeDataChunks("data: {\"test\": \"value\"}\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 1);
}

TEST_F(StreamToMetadataFilterTest, ContentTypeWithParameters) {
  // Content-type matching strips parameters, so "text/event-stream; charset=utf-8"
  // should match "text/event-stream" in allowed_content_types.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(response_headers_with_params_, false));

  const std::string data =
      "data: {\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::kNumberValue);
  EXPECT_EQ(metadata.number_value(), 30);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 0);
}

TEST_F(StreamToMetadataFilterTest, BadContentTypeTriggersOnError) {
  // Create config with on_error to verify it executes when content-type mismatches
  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  auto* rules = proto_config.mutable_response_rules();
  rules->set_format(envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::
                        SERVER_SENT_EVENTS);

  auto* rule = rules->add_rules();
  rule->mutable_selector()->mutable_json_path()->add_path("usage");
  rule->mutable_selector()->mutable_json_path()->add_path("total_tokens");

  auto* on_error = rule->add_on_error();
  on_error->set_metadata_namespace("envoy.lb");
  on_error->set_key("tokens");
  on_error->mutable_value()->set_number_value(-1);

  config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

  // Send response with wrong content-type
  Http::TestResponseHeaderMapImpl bad_headers{{"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(bad_headers, false));
  addEncodeDataChunks("data: {\"test\": \"value\"}\n\n", true);

  // on_error should have executed with fallback value
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), -1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 1);
}

TEST_F(StreamToMetadataFilterTest, BasicTokenExtraction) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data =
      "data: {\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::kNumberValue);
  EXPECT_EQ(metadata.number_value(), 30);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.no_data_field"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.invalid_json"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 0);
}

TEST_F(StreamToMetadataFilterTest, MultipleEventsStopOnFirstMatch) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"id\":\"1\",\"delta\":{\"content\":\"Hello\"}}\n\n");
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n");
  addEncodeDataChunks("data: [DONE]\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, MultipleEventsInSingleChunk) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"id\":\"1\"}\n\n"
                           "data: {\"usage\":{\"total_tokens\":30}}\n\n"
                           "data: [DONE]\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, EventSplitAcrossChunks) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usage\":{\"tota");
  addEncodeDataChunks("l_tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, EventSplitAcrossThreeChunks) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usa");
  addEncodeDataChunks("ge\":{\"total_to");
  addEncodeDataChunks("kens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, MultipleMetadataNamespaces) {
  setupFilter(multi_namespace_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto metadata1 = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata1.number_value(), 30);

  auto metadata2 = getMetadata("envoy.audit", "token_count");
  EXPECT_EQ(metadata2.number_value(), 30);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, PreserveExistingMetadata) {
  setupFilter(preserve_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Set existing metadata
  Protobuf::Struct existing_metadata;
  (*existing_metadata.mutable_fields())["tokens"].set_number_value(100);
  encoder_callbacks_.streamInfo().setDynamicMetadata("envoy.lb", existing_metadata);

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  // Should still be 100
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 100);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.preserved_existing_metadata"), 1);
}

TEST_F(StreamToMetadataFilterTest, OverwriteExistingMetadata) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Set existing metadata
  Protobuf::Struct existing_metadata;
  (*existing_metadata.mutable_fields())["tokens"].set_number_value(100);
  encoder_callbacks_.streamInfo().setDynamicMetadata("envoy.lb", existing_metadata);

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  // Should be overwritten to 30
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.preserved_existing_metadata"), 0);
}

TEST_F(StreamToMetadataFilterTest, MultipleRules) {
  setupFilter(multi_rule_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"model\":\"gpt-4\",\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 30);

  auto model = getMetadata("envoy.lb", "model_name");
  EXPECT_EQ(model.string_value(), "gpt-4");

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 2);
}

TEST_F(StreamToMetadataFilterTest, NoDataField) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("event: message\nid: 123\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.no_data_field"), 1);
}

TEST_F(StreamToMetadataFilterTest, InvalidJson) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: [DONE]\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.invalid_json"), 1);
}

TEST_F(StreamToMetadataFilterTest, SelectorNotFound) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"other_field\":\"value\"}\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
}

TEST_F(StreamToMetadataFilterTest, PartialSelectorPath) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Has 'usage' but not 'total_tokens'
  addEncodeDataChunks("data: {\"usage\":{\"prompt_tokens\":10}}\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
}

TEST_F(StreamToMetadataFilterTest, CRLFLineEndings) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"usage\":{\"total_tokens\":30}}\r\n\r\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, CRLineEndings) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"usage\":{\"total_tokens\":30}}\r\r";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, MixedLineEndings) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"usage\":{\"total_tokens\":30}}\r\n"
                           "event: usage\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, CRLFSplitAcrossChunks) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\r");
  addEncodeDataChunks("\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, NoSpaceAfterColon) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data:{\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, CommentLines) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = ": this is a comment\n"
                           "data: {\"usage\":{\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, CommentOnlyEvent) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks(": keep-alive\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.no_data_field"), 1);
}

TEST_F(StreamToMetadataFilterTest, DataFieldAfterOtherFields) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "event: usage\n"
                           "id: 12345\n"
                           "data: {\"usage\":{\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, MultipleDataFieldsConcatenated) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Multiple data fields should be concatenated with newline
  const std::string data = "data: {\"usage\":{\n"
                           "data: \"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, UnterminatedEventAtStreamEnd) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Event without trailing blank line
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}", true);

  // Event is incomplete, should not be processed
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
}

TEST_F(StreamToMetadataFilterTest, EmptyDataChunk) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("", false);
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, StopProcessingDisabled) {
  setupFilter(no_stop_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Even after finding a match, should continue processing
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n");
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":40}}\n\n", true);

  // Should have processed both events
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 40); // Last write wins
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 2);
}

TEST_F(StreamToMetadataFilterTest, StringValueType) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["model"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "model_name"
            type: STRING
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"model\":\"gpt-4\"}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "model_name");
  EXPECT_EQ(metadata.string_value(), "gpt-4");
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, ProtobufValueType) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["value"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "test"
            type: PROTOBUF_VALUE
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"value\":42}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "test");
  EXPECT_EQ(metadata.number_value(), 42);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, BooleanValue) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["enabled"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "flag"
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"enabled\":true}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "flag");
  EXPECT_EQ(metadata.bool_value(), true);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, NestedObjectValue) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "usage_obj"
            type: STRING
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"usage\":{\"tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "usage_obj");
  EXPECT_TRUE(metadata.has_string_value());
  EXPECT_NE(metadata.string_value().find("tokens"), std::string::npos);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, DeepNestedPath) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["level1", "level2", "level3", "value"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "deep_value"
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"level1\":{\"level2\":{\"level3\":{\"value\":99}}}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "deep_value");
  EXPECT_EQ(metadata.number_value(), 99);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, NullValueInJson) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "value"
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"usage\":null}\n\n", true);

  // Should fail to extract null value
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
}

TEST_F(StreamToMetadataFilterTest, ComplexRealWorldScenario) {
  setupFilter(multi_rule_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Simulate real OpenAI-like streaming response
  addEncodeDataChunks(
      "data: {\"id\":\"1\",\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4\","
      "\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n");
  addEncodeDataChunks(
      "data: {\"id\":\"2\",\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4\","
      "\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n");
  addEncodeDataChunks(
      "data: {\"id\":\"3\",\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4\","
      "\"choices\":[],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,"
      "\"total_tokens\":30}}\n\n");
  addEncodeDataChunks("data: [DONE]\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 30);

  auto model = getMetadata("envoy.lb", "model_name");
  EXPECT_EQ(model.string_value(), "gpt-4");

  // First rule matches once and stops (tokens), second rule matches multiple times
  EXPECT_GE(findCounter("stream_to_metadata.resp.success"), 2);
}

TEST_F(StreamToMetadataFilterTest, EventExceedsMaxSize) {
  const std::string config_with_small_limit = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    max_event_size: 100
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
  )EOF";

  setupFilter(config_with_small_limit);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send a large chunk without event delimiter (blank line) that exceeds 100 bytes
  std::string large_data = "data: {\"usage\":{\"total_tokens\":30";
  while (large_data.size() < 110) {
    large_data += ",\"extra\":\"padding\"";
  }
  addEncodeDataChunks(large_data, false);

  // Buffer should exceed limit and be discarded
  EXPECT_EQ(findCounter("stream_to_metadata.resp.event_too_large"), 1);

  // Now send a valid event, it should process normally
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":42}}\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 42);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, EventWithinMaxSize) {
  const std::string config_with_large_limit = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    max_event_size: 1000
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
  )EOF";

  setupFilter(config_with_large_limit);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send event that's under the limit
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":50}}\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 50);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.event_too_large"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, MaxSizeDisabled) {
  const std::string config_no_limit = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    max_event_size: 0
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
  )EOF";

  setupFilter(config_no_limit);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send a very large chunk without limit
  std::string large_data = "data: {\"usage\":{\"total_tokens\":30";
  while (large_data.size() < 10000) {
    large_data += ",\"extra\":\"x\"";
  }
  large_data += "}}\n\n";
  addEncodeDataChunks(large_data, true);

  // Should process successfully even though it's large
  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 30);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.event_too_large"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, CustomContentTypes) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    allowed_content_types:
      - "application/stream+json"
      - "text/custom-stream"
    rules:
      - selector:
          json_path:
            path: ["value"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
  )EOF";
  setupFilter(config);

  // Test first custom content type
  Http::TestResponseHeaderMapImpl custom_headers1{{"content-type", "application/stream+json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(custom_headers1, false));
  addEncodeDataChunks("data: {\"value\":123}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.number_value(), 123);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 0);
}

TEST_F(StreamToMetadataFilterTest, StringToNumberConversionFailure) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["value"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
            type: NUMBER
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // String value that cannot be converted to number
  addEncodeDataChunks("data: {\"value\":\"not-a-number\"}\n\n", true);

  // Should succeed in extraction but fail in conversion, so no metadata written
  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.kind_case(), 0); // Not set

  // The rule application succeeds but writeMetadata logs warning
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, BoolToNumberConversion) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["flag"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
            type: NUMBER
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"flag\":true}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.number_value(), 1.0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, BoolToStringConversion) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["flag"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
            type: STRING
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"flag\":false}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.string_value(), "false");
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, NumberToStringConversion) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["count"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
            type: STRING
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"count\":42}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.string_value(), "42");
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, DoubleToStringConversion) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["value"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
            type: STRING
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"value\":3.14}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.string_value(), "3.14");
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, FieldWithoutColon) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Field name without colon (treated as field with empty value per SSE spec)
  const std::string data = "data\n\n";
  addEncodeDataChunks(data, true);

  // Empty data field
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.no_data_field"), 1);
}

TEST_F(StreamToMetadataFilterTest, IntermediatePathNotObject) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // 'usage' is a string, not an object, so can't traverse to 'total_tokens'
  addEncodeDataChunks("data: {\"usage\":\"not-an-object\"}\n\n", true);

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
}

TEST_F(StreamToMetadataFilterTest, StopProcessingOnMatch) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
        stop_processing_on_match: true
  )EOF";
  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // First event matches and should stop processing
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n");

  // This should not be processed due to stop_processing_on_match
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":99}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30); // First value, not 99
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, IntegerValueExtraction) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["count"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "count_num"
            type: NUMBER
          - metadata_namespace: "envoy.lb"
            key: "count_str"
            type: STRING
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  addEncodeDataChunks("data: {\"count\":42}\n\n", true);

  // Integer converted to number
  auto metadata_num = getMetadata("envoy.lb", "count_num");
  EXPECT_EQ(metadata_num.number_value(), 42.0);

  // Integer converted to string
  auto metadata_str = getMetadata("envoy.lb", "count_str");
  EXPECT_EQ(metadata_str.string_value(), "42");

  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, FormatGetter) {
  // Verify config format is accessible
  EXPECT_EQ(config_->format(), envoy::extensions::filters::http::stream_to_metadata::v3::
                                   StreamToMetadata::SERVER_SENT_EVENTS);
}

TEST_F(StreamToMetadataFilterTest, StringToNumberConversionSuccess) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["price"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "price_as_number"
            type: NUMBER
  )EOF";
  setupFilter(config);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // JSON field with string value that contains a valid number
  addEncodeDataChunks("data: {\"price\":\"123.45\"}\n\n", true);

  // String should be parsed and converted to number
  auto metadata = getMetadata("envoy.lb", "price_as_number");
  EXPECT_EQ(metadata.number_value(), 123.45);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, EventWithEmptyLines) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // SSE event with empty lines (per SSE spec, empty lines are event delimiters)
  const std::string data = "\n"
                           "data: {\"usage\":{\"total_tokens\":42}}\n"
                           "\n"
                           "\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 42);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, EventStartingWithEmptyLine) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Event starting with empty line followed by data
  // This ensures empty line parsing returns {"", ""} correctly
  const std::string data = "\n\ndata: {\"usage\":{\"total_tokens\":55}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 55);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, ContentTypeWithMultipleParameters) {
  // Test content-type with multiple parameters
  Http::TestResponseHeaderMapImpl headers_multi_params{
      {"content-type", "text/event-stream; charset=utf-8; boundary=foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(headers_multi_params, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":42}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 42);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 0);
}

TEST_F(StreamToMetadataFilterTest, ContentTypeWithSpaceBeforeSemicolon) {
  // Test content-type with space before semicolon
  Http::TestResponseHeaderMapImpl headers_space{
      {"content-type", "text/event-stream ; charset=utf-8"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers_space, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":99}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 99);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, ContentTypeWithTrailingSpaces) {
  Http::TestResponseHeaderMapImpl headers_trailing{{"content-type", "text/event-stream  "}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers_trailing, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":77}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 77);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

TEST_F(StreamToMetadataFilterTest, ContentTypeStillRejectsWrongMediaType) {
  // Ensure parameter stripping doesn't accept wrong media types
  Http::TestResponseHeaderMapImpl wrong_type{{"content-type", "application/json; charset=utf-8"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(wrong_type, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":88}}\n\n", true);

  // Should still be rejected
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), 0); // Not set
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 1);
}

TEST_F(StreamToMetadataFilterTest, ConfiguredContentTypeWithParametersNormalized) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    allowed_content_types:
      - "text/event-stream; charset=utf-8"
    rules:
      - selector:
          json_path:
            path: ["value"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "result"
  )EOF";
  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers_no_params{{"content-type", "text/event-stream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers_no_params, false));

  addEncodeDataChunks("data: {\"value\":123}\n\n", true);

  // Should match because both are normalized to "text/event-stream"
  auto metadata = getMetadata("envoy.lb", "result");
  EXPECT_EQ(metadata.number_value(), 123);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 0);
}

// Test on_missing: writes fallback value when selector path not found
TEST_F(StreamToMetadataFilterTest, OnMissing) {
  // Create config programmatically to properly set protobuf Value
  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  auto* rules = proto_config.mutable_response_rules();
  rules->set_format(envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::
                        SERVER_SENT_EVENTS);

  auto* rule = rules->add_rules();
  rule->mutable_selector()->mutable_json_path()->add_path("usage");
  rule->mutable_selector()->mutable_json_path()->add_path("total_tokens");

  auto* on_present = rule->add_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->set_type(
      envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::NUMBER);

  auto* on_missing = rule->add_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");
  on_missing->mutable_value()->set_number_value(-1);

  config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send event without "usage" field. Should trigger on_missing.
  addEncodeDataChunks(std::string("data: {\"model\": \"gpt-4\"}") + std::string(delimiter_), true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), -1); // Fallback value
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
}

// Test on_error: writes fallback value when JSON parsing fails
TEST_F(StreamToMetadataFilterTest, OnErrorJsonParseFails) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
        on_error:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            value:
              number_value: 0
  )EOF";

  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send malformed JSON. Should trigger on_error.
  addEncodeDataChunks(std::string("data: {malformed json}") + std::string(delimiter_), true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 0); // Fallback value
  EXPECT_EQ(findCounter("stream_to_metadata.resp.invalid_json"), 1);
}

// Test on_error: writes fallback value when data field is missing
TEST_F(StreamToMetadataFilterTest, OnErrorNoDataField) {
  // Create config programmatically to properly set protobuf Value
  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  auto* rules = proto_config.mutable_response_rules();
  rules->set_format(envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::
                        SERVER_SENT_EVENTS);

  auto* rule = rules->add_rules();
  rule->mutable_selector()->mutable_json_path()->add_path("usage");
  rule->mutable_selector()->mutable_json_path()->add_path("total_tokens");

  auto* on_error = rule->add_on_error();
  on_error->set_metadata_namespace("envoy.lb");
  on_error->set_key("tokens");
  on_error->mutable_value()->set_string_value("error");

  config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send event with no data field. Should trigger on_error.
  addEncodeDataChunks(std::string("event: ping") + std::string(delimiter_), true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.string_value(), "error"); // Fallback value
  EXPECT_EQ(findCounter("stream_to_metadata.resp.no_data_field"), 1);
}

// Test hardcoded value in on_present: uses descriptor.value instead of extracted
TEST_F(StreamToMetadataFilterTest, OnPresentWithHardcodedValue) {
  // Create config programmatically to properly set protobuf Value
  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  auto* rules = proto_config.mutable_response_rules();
  rules->set_format(envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::
                        SERVER_SENT_EVENTS);

  auto* rule = rules->add_rules();
  rule->mutable_selector()->mutable_json_path()->add_path("usage");
  rule->mutable_selector()->mutable_json_path()->add_path("total_tokens");

  auto* on_present = rule->add_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->mutable_value()->set_number_value(999);

  config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Extracted value is 42, but hardcoded value 999 should be used
  addEncodeDataChunks(
      std::string("data: {\"usage\": {\"total_tokens\": 42}}") + std::string(delimiter_), true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 999); // Hardcoded value, not 42
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
}

// Test on_error doesn't execute if on_present already executed (even with prior errors)
TEST_F(StreamToMetadataFilterTest, OnErrorDoesNotOverwriteOnPresent) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
        on_error:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            value:
              number_value: 0
  )EOF";

  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Event 1: Error (malformed JSON)
  addEncodeDataChunks(std::string("data: {malformed json}") + std::string(delimiter_), false);

  // Event 2: Success (good value)
  addEncodeDataChunks(
      std::string("data: {\"usage\": {\"total_tokens\": 42}}") + std::string(delimiter_), false);

  // Event 3: Error again
  addEncodeDataChunks(std::string("data: {another error}") + std::string(delimiter_), true);

  // Should have the good value, not the error fallback
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 42); // Good value preserved
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.invalid_json"), 2);
}

// Test on_error takes priority over on_missing
TEST_F(StreamToMetadataFilterTest, OnErrorPriorityOverOnMissing) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_missing:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            value:
              number_value: -1
        on_error:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            value:
              number_value: 0
  )EOF";

  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Event 1: Error
  addEncodeDataChunks(std::string("data: {malformed json}") + std::string(delimiter_), false);

  // Event 2: Valid JSON but missing path
  addEncodeDataChunks(std::string("data: {\"model\": \"gpt-4\"}") + std::string(delimiter_), true);

  // Should use on_error (priority) not on_missing
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 0); // on_error value, not -1
  EXPECT_EQ(findCounter("stream_to_metadata.resp.invalid_json"), 1);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
}

TEST_F(StreamToMetadataFilterTest, TrailersFinalizesRules) {
  // Create config programmatically to ensure on_missing value is set correctly
  envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata proto_config;
  auto* rules = proto_config.mutable_response_rules();
  rules->set_format(envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::
                        SERVER_SENT_EVENTS);

  auto* rule = rules->add_rules();
  rule->mutable_selector()->mutable_json_path()->add_path("usage");
  rule->mutable_selector()->mutable_json_path()->add_path("total_tokens");

  auto* on_missing = rule->add_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");
  on_missing->mutable_value()->set_number_value(-1);

  config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send data with end_stream=false (trailers will follow)
  addEncodeDataChunks(std::string("data: {\"model\": \"gpt-4\"}") + std::string(delimiter_), false);

  // At this point, on_missing should NOT be executed yet
  auto metadata_before = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata_before.kind_case(), 0);

  // Send trailers
  Http::TestResponseTrailerMapImpl trailers{{"x-test-trailer", "value"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // After trailers, on_missing should have been executed
  EXPECT_EQ(findCounter("stream_to_metadata.resp.selector_not_found"), 1);
  auto metadata_after = getMetadata("envoy.lb", "tokens");
  EXPECT_NE(metadata_after.kind_case(), 0);
  EXPECT_EQ(metadata_after.number_value(), -1);
}

TEST_F(StreamToMetadataFilterTest, TrailersWithContentTypeMismatch) {
  const std::string config = R"EOF(
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_error:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            value:
              number_value: 0
  )EOF";

  setupFilter(config);
  Http::TestResponseHeaderMapImpl wrong_headers{{":status", "200"},
                                                {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(wrong_headers, false));

  // Send some data
  addEncodeDataChunks("{\"some\": \"json\"}", false);

  // Send trailers
  Http::TestResponseTrailerMapImpl trailers{{"x-test-trailer", "value"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // Should execute on_error due to content-type mismatch
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 0);
  EXPECT_EQ(findCounter("stream_to_metadata.resp.mismatched_content_type"), 1);
}

} // namespace
} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
