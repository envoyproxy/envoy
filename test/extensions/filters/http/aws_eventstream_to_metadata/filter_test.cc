#include <zlib.h>

#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/common/config/metadata.h"
#include "source/extensions/common/aws/eventstream/eventstream_parser.h"
#include "source/extensions/filters/http/aws_eventstream_to_metadata/config.h"
#include "source/extensions/filters/http/aws_eventstream_to_metadata/filter.h"

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
namespace AwsEventstreamToMetadata {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

class AwsEventstreamToMetadataFilterTest : public testing::Test {
public:
  AwsEventstreamToMetadataFilterTest() : stream_info_(time_source_) {}

  void SetUp() override {
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    setupFilter(basic_config_);
  }

  void setupFilter(const std::string& yaml) {
    envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
        proto_config;
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

  // Helper to build an EventStream message with the given payload.
  // This creates a properly formatted binary EventStream message.
  static std::string buildEventstreamMessage(const std::string& payload) {
    // EventStream message format:
    // - Prelude (12 bytes): total_length(4) + headers_length(4) + prelude_crc(4)
    // - Headers (variable)
    // - Payload (variable)
    // - Message CRC (4 bytes)

    // For simplicity, we'll create a message with no headers.
    const uint32_t headers_length = 0;
    const uint32_t payload_length = payload.size();
    const uint32_t total_length = 12 + headers_length + payload_length + 4; // prelude + headers +
                                                                            // payload + trailer

    std::string message;
    message.resize(total_length);

    auto* data = reinterpret_cast<uint8_t*>(message.data());

    // Write total_length (big-endian)
    data[0] = (total_length >> 24) & 0xFF;
    data[1] = (total_length >> 16) & 0xFF;
    data[2] = (total_length >> 8) & 0xFF;
    data[3] = total_length & 0xFF;

    // Write headers_length (big-endian)
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    data[7] = 0;

    // Compute prelude CRC (CRC32 of first 8 bytes)
    uint32_t prelude_crc = crc32(0, data, 8);
    data[8] = (prelude_crc >> 24) & 0xFF;
    data[9] = (prelude_crc >> 16) & 0xFF;
    data[10] = (prelude_crc >> 8) & 0xFF;
    data[11] = prelude_crc & 0xFF;

    // Copy payload
    std::memcpy(data + 12, payload.data(), payload_length);

    // Compute message CRC (CRC32 of everything except the last 4 bytes)
    uint32_t message_crc = crc32(0, data, total_length - 4);
    data[total_length - 4] = (message_crc >> 24) & 0xFF;
    data[total_length - 3] = (message_crc >> 16) & 0xFF;
    data[total_length - 2] = (message_crc >> 8) & 0xFF;
    data[total_length - 1] = message_crc & 0xFF;

    return message;
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

  Event::SimulatedTimeSystem time_source_;
  Stats::TestUtil::TestStore stats_store_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  TestStreamInfo stream_info_;

  std::shared_ptr<FilterConfig> config_;
  std::unique_ptr<Filter> filter_;
};

TEST_F(AwsEventstreamToMetadataFilterTest, MismatchedContentType) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.mismatched_content_type"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, MissingContentType) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.mismatched_content_type"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, ValidEventstreamContentType) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.mismatched_content_type"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, ContentTypeWithCharset) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"content-type", "application/vnd.amazon.eventstream; charset=utf-8"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.mismatched_content_type"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, BasicMetadataExtraction) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamToMetadataFilterTest, MultipleMessages) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send first message (no match)
  Buffer::OwnedImpl data1(buildEventstreamMessage(R"({"text": "hello"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // Send second message (match)
  Buffer::OwnedImpl data2(buildEventstreamMessage(R"({"usage": {"total_tokens": 250}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(250, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamToMetadataFilterTest, EmptyPayload) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(""));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.empty_payload"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, InvalidJsonPayload) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage("not valid json"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.parse_error"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, ChunkedMessage) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string full_message = buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})");

  // Send message in two chunks
  Buffer::OwnedImpl data1(full_message.substr(0, 10));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // No metadata yet (incomplete message)
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  Buffer::OwnedImpl data2(full_message.substr(10));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamToMetadataFilterTest, MultipleMessagesInSingleBuffer) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string msg1 = buildEventstreamMessage(R"({"text": "hello"})");
  std::string msg2 = buildEventstreamMessage(R"({"usage": {"total_tokens": 42}})");

  Buffer::OwnedImpl data(msg1 + msg2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(42, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamToMetadataFilterTest, TrailersFinalizesRules) {
  const std::string config_with_fallback = R"EOF(
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
                value: "0"
                type: NUMBER
  )EOF";

  setupFilter(config_with_fallback);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send a message that doesn't match
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"text": "no match"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));

  // Send trailers to finalize
  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // Fallback should be applied
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_from_fallback"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, EventStreamParseError) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Create a corrupted EventStream message (invalid CRC)
  std::string corrupted_message = buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})");
  // Corrupt the message CRC by flipping some bits in the last 4 bytes
  corrupted_message[corrupted_message.size() - 1] ^= 0xFF;
  corrupted_message[corrupted_message.size() - 2] ^= 0xFF;

  Buffer::OwnedImpl data(corrupted_message);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // EventStream error counter should be incremented
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.eventstream_error"));
  // No metadata should be added since parse failed
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, PreserveExistingMetadata) {
  const std::string config_with_preserve = R"EOF(
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

  setupFilter(config_with_preserve);

  // Pre-populate metadata with existing value
  Protobuf::Struct existing_metadata;
  (*existing_metadata.mutable_fields())["tokens"].set_number_value(999);
  stream_info_.setDynamicMetadata("envoy.lb", existing_metadata);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Preserved existing metadata counter should be incremented
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.preserved_existing_metadata"));
  // No new metadata should be added
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  // Original value should be preserved
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(999, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamToMetadataFilterTest, StopProcessingEarly) {
  const std::string config_with_max_matches = R"EOF(
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

  setupFilter(config_with_max_matches);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send first message that matches
  std::string msg1 = buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})");
  // Send second message that also matches (but should be ignored due to max_matches=1)
  std::string msg2 = buildEventstreamMessage(R"({"usage": {"total_tokens": 200}})");

  Buffer::OwnedImpl data(msg1 + msg2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Only one metadata_added since max_matches=1 stops processing
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  // First matched value should be stored
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamToMetadataFilterTest, ContentTypeCaseInsensitive) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "APPLICATION/VND.AMAZON.EVENTSTREAM"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.mismatched_content_type"));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamToMetadataFilterTest, EmptyDataChunk) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send empty data chunk
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, false));

  // Send actual data
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

// Test that encodeData skips processing when processing_complete_ is already true.
TEST_F(AwsEventstreamToMetadataFilterTest, SkipProcessingAfterComplete) {
  const std::string config_with_max_matches = R"EOF(
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

  setupFilter(config_with_max_matches);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // First message triggers match and sets processing_complete_
  Buffer::OwnedImpl data1(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  // Second message should be completely skipped (processing_complete_ is true)
  Buffer::OwnedImpl data2(buildEventstreamMessage(R"({"usage": {"total_tokens": 200}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  // Still only 1 metadata_added
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

// Test encodeTrailers when content type didn't match (should not finalize).
TEST_F(AwsEventstreamToMetadataFilterTest, TrailersSkippedWhenContentTypeMismatched) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // No fallback should be applied since content type didn't match
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_from_fallback"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

// Test encodeTrailers when processing is already complete (should not double-finalize).
TEST_F(AwsEventstreamToMetadataFilterTest, TrailersSkippedWhenProcessingComplete) {
  const std::string config_with_max_matches = R"EOF(
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

  setupFilter(config_with_max_matches);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Match triggers processing_complete_ via stop_processing
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));

  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  // Trailers should not trigger another finalize
  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // Still only 1 metadata_added (no double finalize)
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

// Test preserve_existing when namespace exists but with a different key (should write).
TEST_F(AwsEventstreamToMetadataFilterTest, PreserveExistingDifferentKey) {
  const std::string config_with_preserve = R"EOF(
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

  setupFilter(config_with_preserve);

  // Pre-populate metadata with a different key in the same namespace
  Protobuf::Struct existing_metadata;
  (*existing_metadata.mutable_fields())["other_key"].set_number_value(999);
  stream_info_.setDynamicMetadata("envoy.lb", existing_metadata);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Should write since the key "tokens" doesn't exist yet (only "other_key" does)
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.preserved_existing_metadata"));
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
  // Original key should still exist
  EXPECT_EQ(999, metadata.fields().at("other_key").number_value());
}

// Test end_stream=true finalizes rules with on_missing fallback.
TEST_F(AwsEventstreamToMetadataFilterTest, EndStreamFinalizesFallback) {
  const std::string config_with_fallback = R"EOF(
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
                  number_value: 0
  )EOF";

  setupFilter(config_with_fallback);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send a non-matching message with end_stream=true to trigger finalize
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"text": "no match"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Fallback should be applied at end of stream
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_from_fallback"));
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

// Test finalizeRules with no deferred actions (all rules matched).
TEST_F(AwsEventstreamToMetadataFilterTest, FinalizeWithNoDeferredActions) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send message that matches the rule
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Rule matched, so finalization at end_stream should produce no deferred actions
  EXPECT_EQ(1, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_from_fallback"));
}

// Test ContentType with leading/trailing whitespace.
TEST_F(AwsEventstreamToMetadataFilterTest, ContentTypeWithWhitespace) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"},
      {"content-type", "  application/vnd.amazon.eventstream  ; charset=utf-8"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.mismatched_content_type"));
}

// Test that encodeData with non-matching content type still returns Continue.
TEST_F(AwsEventstreamToMetadataFilterTest, EncodeDataSkipsWhenContentTypeMismatched) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "text/plain"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // No processing should happen
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.empty_payload"));
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.parse_error"));
}

// Test multiple deferred fallback actions at end of stream.
TEST_F(AwsEventstreamToMetadataFilterTest, MultipleDeferredFallbackActions) {
  envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
      proto_config;
  auto* response_rules = proto_config.mutable_response_rules();
  auto* content_parser = response_rules->mutable_content_parser();
  content_parser->set_name("envoy.content_parsers.json");

  envoy::extensions::content_parsers::json::v3::JsonContentParser json_config;

  // Rule 1: usage.total_tokens with on_missing fallback value 0
  auto* rule1 = json_config.add_rules()->mutable_rule();
  rule1->add_selectors()->set_key("usage");
  rule1->add_selectors()->set_key("total_tokens");
  auto* on_missing1 = rule1->mutable_on_missing();
  on_missing1->set_metadata_namespace("envoy.lb");
  on_missing1->set_key("tokens");
  on_missing1->mutable_value()->set_number_value(0);

  // Rule 2: usage.input_tokens with on_missing fallback value 99
  auto* rule2 = json_config.add_rules()->mutable_rule();
  rule2->add_selectors()->set_key("usage");
  rule2->add_selectors()->set_key("input_tokens");
  auto* on_missing2 = rule2->mutable_on_missing();
  on_missing2->set_metadata_namespace("envoy.lb");
  on_missing2->set_key("input_tokens");
  on_missing2->mutable_value()->set_number_value(99);

  content_parser->mutable_typed_config()->PackFrom(json_config);

  config_ = std::make_shared<FilterConfig>(proto_config, context_);
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send a message that doesn't match either rule
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"text": "hello"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Both fallbacks should be applied
  EXPECT_EQ(2, findCounter("aws_eventstream_to_metadata.resp.json.metadata_from_fallback"));
  EXPECT_EQ(2, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(0, metadata.fields().at("tokens").number_value());
  EXPECT_EQ(99, metadata.fields().at("input_tokens").number_value());
}

// Test on_missing without a value field does not write metadata (covers !action.value.has_value()).
TEST_F(AwsEventstreamToMetadataFilterTest, OnMissingWithoutValueDoesNotWriteMetadata) {
  envoy::extensions::filters::http::aws_eventstream_to_metadata::v3::AwsEventstreamToMetadata
      proto_config;
  auto* response_rules = proto_config.mutable_response_rules();
  auto* content_parser = response_rules->mutable_content_parser();
  content_parser->set_name("envoy.content_parsers.json");

  envoy::extensions::content_parsers::json::v3::JsonContentParser json_config;
  auto* rule = json_config.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("nonexistent");
  rule->add_selectors()->set_key("path");

  // on_missing with no value set
  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");

  content_parser->mutable_typed_config()->PackFrom(json_config);

  config_ = std::make_shared<FilterConfig>(proto_config, context_);
  filter_ = std::make_unique<Filter>(config_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send a message that doesn't match the selector path
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"model": "claude-3"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // on_missing fires but has no value — metadata should not be written
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

// Test that string-to-number conversion failure produces KIND_NOT_SET and does not write metadata.
TEST_F(AwsEventstreamToMetadataFilterTest, StringToNumberConversionFailureDoesNotWriteMetadata) {
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

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send JSON with a string value that cannot be converted to a number
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"model": "gpt-4-turbo"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // The string "gpt-4-turbo" cannot be parsed as a number, so kind_case will be KIND_NOT_SET
  // and metadata should not be written
  EXPECT_EQ(0, findCounter("aws_eventstream_to_metadata.resp.json.metadata_added"));
}

} // namespace
} // namespace AwsEventstreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
