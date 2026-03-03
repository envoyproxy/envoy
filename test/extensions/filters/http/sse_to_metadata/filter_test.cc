#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/common/config/metadata.h"
#include "source/extensions/filters/http/sse_to_metadata/config.h"
#include "source/extensions/filters/http/sse_to_metadata/filter.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SseToMetadata {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

class SseToMetadataFilterTest : public testing::Test {
public:
  SseToMetadataFilterTest()
      : stream_info_(time_source_, nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  void SetUp() override {
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    setupFilter(basic_config_);
  }

  void setupFilter(const std::string& yaml) {
    envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    config_ = std::make_shared<FilterConfig>(proto_config, context_);
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
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
  )EOF";

  const std::string multi_namespace_config_ = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.audit"
                key: "token_count"
                type: NUMBER
  )EOF";

  const std::string preserve_config_ = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
                preserve_existing_metadata_value: true
  )EOF";

  const std::string multi_rule_config_ = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "model"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "model_name"
                type: STRING
  )EOF";

  const std::string no_stop_config_ = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
  )EOF";

  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::shared_ptr<FilterConfig> config_;
  std::unique_ptr<Filter> filter_;

  Http::TestResponseHeaderMapImpl response_headers_{{"content-type", "text/event-stream"}};
  Http::TestResponseHeaderMapImpl response_headers_with_params_{
      {"content-type", "text/event-stream; charset=utf-8"}};
  Buffer::OwnedImpl response_data_;

  static constexpr absl::string_view delimiter_ = "\n\n";
};

TEST_F(SseToMetadataFilterTest, BadContentType) {
  Http::TestResponseHeaderMapImpl bad_headers{{"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(bad_headers, false));

  addEncodeDataChunks("data: {\"test\": \"value\"}\n\n", true);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 1);
}

TEST_F(SseToMetadataFilterTest, NoContentTypeHeader) {
  Http::TestResponseHeaderMapImpl no_ct_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(no_ct_headers, false));

  addEncodeDataChunks("data: {\"test\": \"value\"}\n\n", true);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 1);
}

TEST_F(SseToMetadataFilterTest, ContentTypeWithParameters) {
  // Content-type matching strips parameters, so "text/event-stream; charset=utf-8"
  // should match the required "text/event-stream" content type.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(response_headers_with_params_, false));

  const std::string data =
      "data: {\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::kNumberValue);
  EXPECT_EQ(metadata.number_value(), 30);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 0);
}

TEST_F(SseToMetadataFilterTest, BadContentTypeSkipsProcessing) {
  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  auto* response_rules = proto_config.mutable_response_rules();

  // Set up content parser config
  auto* content_parser = response_rules->mutable_content_parser();
  content_parser->set_name("envoy.content_parsers.json");

  envoy::extensions::content_parsers::json::v3::JsonContentParser json_config;
  auto* rule = json_config.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("usage");
  rule->add_selectors()->set_key("total_tokens");

  auto* on_error = rule->mutable_on_error();
  on_error->set_metadata_namespace("envoy.lb");
  on_error->set_key("tokens");
  on_error->mutable_value()->set_number_value(-1);

  content_parser->mutable_typed_config()->PackFrom(json_config);

  config_ = std::make_shared<FilterConfig>(proto_config, context_);
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

  // Send response with wrong content-type
  Http::TestResponseHeaderMapImpl bad_headers{{"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(bad_headers, false));
  addEncodeDataChunks("data: {\"test\": \"value\"}\n\n", true);

  // Filter skips processing
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::KIND_NOT_SET);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 1);
}

TEST_F(SseToMetadataFilterTest, MultipleEventsStopOnFirstMatch) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"id\":\"1\",\"delta\":{\"content\":\"Hello\"}}\n\n");
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n");
  addEncodeDataChunks("data: [DONE]\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, MultipleEventsInSingleChunk) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"id\":\"1\"}\n\n"
                           "data: {\"usage\":{\"total_tokens\":30}}\n\n"
                           "data: [DONE]\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, EventSplitAcrossChunks) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usage\":{\"tota");
  addEncodeDataChunks("l_tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, EventSplitAcrossThreeChunks) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usa");
  addEncodeDataChunks("ge\":{\"total_to");
  addEncodeDataChunks("kens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, MultipleMetadataNamespaces) {
  setupFilter(multi_namespace_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto metadata1 = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata1.number_value(), 30);

  auto metadata2 = getMetadata("envoy.audit", "token_count");
  EXPECT_EQ(metadata2.number_value(), 30);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 2);
}

TEST_F(SseToMetadataFilterTest, PreserveExistingMetadata) {
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

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.preserved_existing_metadata"), 1);
}

TEST_F(SseToMetadataFilterTest, OverwriteExistingMetadata) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Set existing metadata
  Protobuf::Struct existing_metadata;
  (*existing_metadata.mutable_fields())["tokens"].set_number_value(100);
  encoder_callbacks_.streamInfo().setDynamicMetadata("envoy.lb", existing_metadata);

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  // Should be overwritten to 30
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.preserved_existing_metadata"), 0);
}

TEST_F(SseToMetadataFilterTest, MultipleRules) {
  setupFilter(multi_rule_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"model\":\"gpt-4\",\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 30);

  auto model = getMetadata("envoy.lb", "model_name");
  EXPECT_EQ(model.string_value(), "gpt-4");

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 2);
}

TEST_F(SseToMetadataFilterTest, NoDataField) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("event: message\nid: 123\n\n", true);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.no_data_field"), 1);
}

TEST_F(SseToMetadataFilterTest, CRLFLineEndings) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"usage\":{\"total_tokens\":30}}\r\n\r\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, CRLineEndings) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"usage\":{\"total_tokens\":30}}\r\r";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, MixedLineEndings) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "data: {\"usage\":{\"total_tokens\":30}}\r\n"
                           "event: usage\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, CRLFSplitAcrossChunks) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\r");
  addEncodeDataChunks("\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, NoSpaceAfterColon) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("data:{\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, CommentLines) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = ": this is a comment\n"
                           "data: {\"usage\":{\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, CommentOnlyEvent) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks(": keep-alive\n\n", true);

  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.no_data_field"), 1);
}

TEST_F(SseToMetadataFilterTest, DataFieldAfterOtherFields) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string data = "event: usage\n"
                           "id: 12345\n"
                           "data: {\"usage\":{\"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, MultipleDataFieldsConcatenated) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Multiple data fields should be concatenated with newline
  const std::string data = "data: {\"usage\":{\n"
                           "data: \"total_tokens\":30}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, UnterminatedEventAtStreamEnd) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Event without trailing blank line
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}", true);

  // Event is incomplete, should not be processed
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
}

TEST_F(SseToMetadataFilterTest, EmptyDataChunk) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  addEncodeDataChunks("", false);
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, StopProcessingDisabled) {
  setupFilter(no_stop_config_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Even after finding a match, should continue processing
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n");
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":40}}\n\n", true);

  // Should have processed both events
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 40); // Last write wins
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 2);
}

TEST_F(SseToMetadataFilterTest, ComplexRealWorldScenario) {
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
  EXPECT_GE(findCounter("sse_to_metadata.resp.json.metadata_added"), 2);
}

TEST_F(SseToMetadataFilterTest, EventExceedsMaxSize) {
  const std::string config_with_small_limit = R"EOF(
  response_rules:
    max_event_size: 100
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
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
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.event_too_large"), 1);

  // Now send a valid event, it should process normally
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":42}}\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 42);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, EventWithinMaxSize) {
  const std::string config_with_large_limit = R"EOF(
  response_rules:
    max_event_size: 1000
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
  )EOF";

  setupFilter(config_with_large_limit);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send event that's under the limit
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":50}}\n\n", true);

  auto tokens = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(tokens.number_value(), 50);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.event_too_large"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, MaxSizeDisabled) {
  const std::string config_no_limit = R"EOF(
  response_rules:
    max_event_size: 0
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
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
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.event_too_large"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, FieldWithoutColon) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Field name without colon (treated as field with empty value per SSE spec)
  const std::string data = "data\n\n";
  addEncodeDataChunks(data, true);

  // Empty data field
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.no_data_field"), 1);
}

TEST_F(SseToMetadataFilterTest, StopProcessingOnMatch) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
            stop_processing_after_matches: 1
  )EOF";
  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // First event matches and should stop processing
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":30}}\n\n");

  // This should not be processed due to stop_processing_on_match
  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":99}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 30); // First value, not 99
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, EventWithEmptyLines) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // SSE event with empty lines (per SSE spec, empty lines are event delimiters)
  const std::string data = "\n"
                           "data: {\"usage\":{\"total_tokens\":42}}\n"
                           "\n"
                           "\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 42);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, EventStartingWithEmptyLine) {
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Event starting with empty line followed by data
  // This ensures empty line parsing returns {"", ""} correctly
  const std::string data = "\n\ndata: {\"usage\":{\"total_tokens\":55}}\n\n";
  addEncodeDataChunks(data, true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 55);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, ContentTypeWithMultipleParameters) {
  // Test content-type with multiple parameters
  Http::TestResponseHeaderMapImpl headers_multi_params{
      {"content-type", "text/event-stream; charset=utf-8; boundary=foo"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(headers_multi_params, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":42}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 42);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 0);
}

TEST_F(SseToMetadataFilterTest, ContentTypeWithSpaceBeforeSemicolon) {
  // Test content-type with space before semicolon
  Http::TestResponseHeaderMapImpl headers_space{
      {"content-type", "text/event-stream ; charset=utf-8"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers_space, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":99}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 99);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, ContentTypeWithTrailingSpaces) {
  Http::TestResponseHeaderMapImpl headers_trailing{{"content-type", "text/event-stream  "}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers_trailing, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":77}}\n\n", true);

  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.number_value(), 77);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
}

TEST_F(SseToMetadataFilterTest, ContentTypeStillRejectsWrongMediaType) {
  // Ensure parameter stripping doesn't accept wrong media types
  Http::TestResponseHeaderMapImpl wrong_type{{"content-type", "application/json; charset=utf-8"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(wrong_type, false));

  addEncodeDataChunks("data: {\"usage\":{\"total_tokens\":88}}\n\n", true);

  // Should still be rejected
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), 0); // Not set
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 0);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 1);
}

TEST_F(SseToMetadataFilterTest, OnErrorJsonParseFails) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
              on_error:
                  metadata_namespace: "envoy.lb"
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
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.parse_error"), 1);
}

// Test that events without a data field are skipped (not treated as errors)
TEST_F(SseToMetadataFilterTest, NoDataFieldSkipsEvent) {
  // Events without a data field (like ping/keepalive) are simply skipped.
  // This is not an error condition - on_error does NOT trigger.
  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  auto* response_rules = proto_config.mutable_response_rules();

  // Set up content parser config
  auto* content_parser = response_rules->mutable_content_parser();
  content_parser->set_name("envoy.content_parsers.json");

  envoy::extensions::content_parsers::json::v3::JsonContentParser json_config;
  auto* rule = json_config.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("usage");
  rule->add_selectors()->set_key("total_tokens");

  auto* on_error = rule->mutable_on_error();
  on_error->set_metadata_namespace("envoy.lb");
  on_error->set_key("tokens");
  on_error->mutable_value()->set_string_value("error");

  content_parser->mutable_typed_config()->PackFrom(json_config);

  config_ = std::make_shared<FilterConfig>(proto_config, context_);
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send event with no data field
  addEncodeDataChunks(std::string("event: ping") + std::string(delimiter_), true);

  // No metadata written
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::KIND_NOT_SET);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.no_data_field"), 1);
}

// Test hardcoded value in on_present: uses descriptor.value instead of extracted
// Test on_error doesn't execute if on_present already executed (even with prior errors)
TEST_F(SseToMetadataFilterTest, OnErrorDoesNotOverwriteOnPresent) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
              on_error:
                  metadata_namespace: "envoy.lb"
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
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.metadata_added"), 1);
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.parse_error"), 2);
}

// Test on_error takes priority over on_missing
TEST_F(SseToMetadataFilterTest, OnErrorPriorityOverOnMissing) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                value:
                  number_value: -1
              on_error:
                metadata_namespace: "envoy.lb"
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
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.parse_error"), 1);
}

TEST_F(SseToMetadataFilterTest, TrailersFinalizesRules) {
  // Create config programmatically to ensure on_missing value is set correctly
  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  auto* response_rules = proto_config.mutable_response_rules();

  // Set up content parser config
  auto* content_parser = response_rules->mutable_content_parser();
  content_parser->set_name("envoy.content_parsers.json");

  envoy::extensions::content_parsers::json::v3::JsonContentParser json_config;
  auto* rule = json_config.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("usage");
  rule->add_selectors()->set_key("total_tokens");

  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");
  on_missing->mutable_value()->set_number_value(-1);

  content_parser->mutable_typed_config()->PackFrom(json_config);

  config_ = std::make_shared<FilterConfig>(proto_config, context_);
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
  auto metadata_after = getMetadata("envoy.lb", "tokens");
  EXPECT_NE(metadata_after.kind_case(), 0);
  EXPECT_EQ(metadata_after.number_value(), -1);
}

TEST_F(SseToMetadataFilterTest, TrailersWithContentTypeMismatch) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_error:
                metadata_namespace: "envoy.lb"
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
  EXPECT_EQ(findCounter("sse_to_metadata.resp.json.mismatched_content_type"), 1);
}

TEST_F(SseToMetadataFilterTest, OnMissingWithoutValueDoesNotWriteMetadata) {
  // Create config programmatically
  envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata proto_config;
  auto* response_rules = proto_config.mutable_response_rules();

  auto* content_parser = response_rules->mutable_content_parser();
  content_parser->set_name("envoy.content_parsers.json");

  envoy::extensions::content_parsers::json::v3::JsonContentParser json_config;
  auto* rule = json_config.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("nonexistent");
  rule->add_selectors()->set_key("path");

  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");

  content_parser->mutable_typed_config()->PackFrom(json_config);

  config_ = std::make_shared<FilterConfig>(proto_config, context_);
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send valid JSON that doesn't have the selector path
  addEncodeDataChunks(std::string("data: {\"model\": \"gpt-4\"}") + std::string(delimiter_), true);

  // on_missing fires but has no value. Metadata should not be written
  auto metadata = getMetadata("envoy.lb", "tokens");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::KIND_NOT_SET);
}

TEST_F(SseToMetadataFilterTest, StringToNumberConversionFailureDoesNotWriteMetadata) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "model"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "model_as_number"
                type: NUMBER
  )EOF";

  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send JSON with a string value that cannot be converted to a number
  addEncodeDataChunks(std::string("data: {\"model\": \"gpt-4-turbo\"}") + std::string(delimiter_),
                      true);

  // The string "gpt-4-turbo" cannot be parsed as a number, so kind_case will be KIND_NOT_SET
  // and metadata should not be written
  auto metadata = getMetadata("envoy.lb", "model_as_number");
  EXPECT_EQ(metadata.kind_case(), Protobuf::Value::KIND_NOT_SET);
}

} // namespace
} // namespace SseToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
