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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
      - selector:
          json_path:
            path: ["model"]
        metadata_descriptors:
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
        metadata_descriptors:
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

  const std::string delimiter_ = "\n\n";
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
  // Content-type matching is exact, so "text/event-stream; charset=utf-8" should NOT match
  // "text/event-stream" in allowed_content_types.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(response_headers_with_params_, false));

  const std::string data =
      "data: {\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  // Verify no metadata was written since content-type didn't match
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), 0); // Not set

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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
        metadata_descriptors:
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
  EXPECT_EQ(metadata.number_value(), 30);                       // First value, not 99
  EXPECT_EQ(findCounter("stream_to_metadata.resp.success"), 1); // Only one success
}

} // namespace
} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
