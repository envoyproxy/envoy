#include <zlib.h>

#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/common/config/metadata.h"
#include "source/extensions/common/aws/eventstream/eventstream_parser.h"
#include "source/extensions/filters/http/aws_eventstream_parser/config.h"
#include "source/extensions/filters/http/aws_eventstream_parser/filter.h"

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
namespace AwsEventstreamParser {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

class AwsEventstreamParserFilterTest : public testing::Test {
public:
  AwsEventstreamParserFilterTest() : stream_info_(time_source_) {}

  void SetUp() override {
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    setupFilter(basic_config_);
  }

  void setupFilter(const std::string& yaml) {
    envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
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

  // Helper to build a single EventStream header in binary format.
  // Supports String (type 7) and Int32 (type 4) header types.
  static std::string buildStringHeader(const std::string& name, const std::string& value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(7); // String type
    header.push_back(static_cast<char>((value.size() >> 8) & 0xFF));
    header.push_back(static_cast<char>(value.size() & 0xFF));
    header.append(value);
    return header;
  }

  static std::string buildInt32Header(const std::string& name, int32_t value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(4); // Int32 type
    header.push_back(static_cast<char>((value >> 24) & 0xFF));
    header.push_back(static_cast<char>((value >> 16) & 0xFF));
    header.push_back(static_cast<char>((value >> 8) & 0xFF));
    header.push_back(static_cast<char>(value & 0xFF));
    return header;
  }

  static std::string buildBoolHeader(const std::string& name, bool value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(value ? 0 : 1); // BoolTrue=0, BoolFalse=1
    return header;
  }

  static std::string buildByteHeader(const std::string& name, int8_t value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(2); // Byte type
    header.push_back(static_cast<char>(value));
    return header;
  }

  static std::string buildShortHeader(const std::string& name, int16_t value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(3); // Short type
    header.push_back(static_cast<char>((value >> 8) & 0xFF));
    header.push_back(static_cast<char>(value & 0xFF));
    return header;
  }

  static std::string buildInt64Header(const std::string& name, int64_t value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(5); // Int64 type
    for (int i = 7; i >= 0; --i) {
      header.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
    return header;
  }

  static std::string buildTimestampHeader(const std::string& name, int64_t value) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(8); // Timestamp type
    for (int i = 7; i >= 0; --i) {
      header.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
    return header;
  }

  static std::string buildByteArrayHeader(const std::string& name, const std::string& bytes) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(6); // ByteArray type
    header.push_back(static_cast<char>((bytes.size() >> 8) & 0xFF));
    header.push_back(static_cast<char>(bytes.size() & 0xFF));
    header.append(bytes);
    return header;
  }

  static std::string buildUuidHeader(const std::string& name, const std::array<uint8_t, 16>& uuid) {
    std::string header;
    header.push_back(static_cast<char>(name.size()));
    header.append(name);
    header.push_back(9); // Uuid type
    header.append(reinterpret_cast<const char*>(uuid.data()), 16);
    return header;
  }

  // Build an EventStream message with headers and payload.
  static std::string buildEventstreamMessageWithHeaders(const std::string& headers_bytes,
                                                        const std::string& payload) {
    const uint32_t headers_length = headers_bytes.size();
    const uint32_t payload_length = payload.size();
    const uint32_t total_length = 12 + headers_length + payload_length + 4;

    std::string message;
    message.resize(total_length);

    auto* data = reinterpret_cast<uint8_t*>(message.data());

    // Write total_length (big-endian)
    data[0] = (total_length >> 24) & 0xFF;
    data[1] = (total_length >> 16) & 0xFF;
    data[2] = (total_length >> 8) & 0xFF;
    data[3] = total_length & 0xFF;

    // Write headers_length (big-endian)
    data[4] = (headers_length >> 24) & 0xFF;
    data[5] = (headers_length >> 16) & 0xFF;
    data[6] = (headers_length >> 8) & 0xFF;
    data[7] = headers_length & 0xFF;

    // Compute prelude CRC
    uint32_t prelude_crc = crc32(0, data, 8);
    data[8] = (prelude_crc >> 24) & 0xFF;
    data[9] = (prelude_crc >> 16) & 0xFF;
    data[10] = (prelude_crc >> 8) & 0xFF;
    data[11] = prelude_crc & 0xFF;

    // Copy headers
    std::memcpy(data + 12, headers_bytes.data(), headers_length);

    // Copy payload
    std::memcpy(data + 12 + headers_length, payload.data(), payload_length);

    // Compute message CRC
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

TEST_F(AwsEventstreamParserFilterTest, MismatchedContentType) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.mismatched_content_type"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamParserFilterTest, MissingContentType) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.mismatched_content_type"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamParserFilterTest, ValidEventstreamContentType) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.mismatched_content_type"));
}

TEST_F(AwsEventstreamParserFilterTest, ContentTypeWithCharset) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"content-type", "application/vnd.amazon.eventstream; charset=utf-8"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.mismatched_content_type"));
}

TEST_F(AwsEventstreamParserFilterTest, BasicMetadataExtraction) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, MultipleMessages) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send first message (no match)
  Buffer::OwnedImpl data1(buildEventstreamMessage(R"({"text": "hello"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // Send second message (match)
  Buffer::OwnedImpl data2(buildEventstreamMessage(R"({"usage": {"total_tokens": 250}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(250, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, EmptyPayload) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(""));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.empty_payload"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamParserFilterTest, InvalidJsonPayload) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage("not valid json"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.parse_error"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamParserFilterTest, ChunkedMessage) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string full_message = buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})");

  // Send message in two chunks
  Buffer::OwnedImpl data1(full_message.substr(0, 10));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // No metadata yet (incomplete message)
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  Buffer::OwnedImpl data2(full_message.substr(10));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, MultipleMessagesInSingleBuffer) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string msg1 = buildEventstreamMessage(R"({"text": "hello"})");
  std::string msg2 = buildEventstreamMessage(R"({"usage": {"total_tokens": 42}})");

  Buffer::OwnedImpl data(msg1 + msg2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(42, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, TrailersFinalizesRules) {
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
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
}

TEST_F(AwsEventstreamParserFilterTest, EventStreamParseError) {
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
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.eventstream_error"));
  // No metadata should be added since parse failed
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamParserFilterTest, PreserveExistingMetadata) {
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
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.preserved_existing_metadata"));
  // No new metadata should be added
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  // Original value should be preserved
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(999, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, StopProcessingEarly) {
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
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  // First matched value should be stored
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, ContentTypeCaseInsensitive) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "APPLICATION/VND.AMAZON.EVENTSTREAM"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.mismatched_content_type"));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

TEST_F(AwsEventstreamParserFilterTest, EmptyDataChunk) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send empty data chunk
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, false));

  // Send actual data
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

// Test that encodeData skips processing when processing_complete_ is already true.
TEST_F(AwsEventstreamParserFilterTest, SkipProcessingAfterComplete) {
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

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  // Second message should be completely skipped (processing_complete_ is true)
  Buffer::OwnedImpl data2(buildEventstreamMessage(R"({"usage": {"total_tokens": 200}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  // Still only 1 metadata_added
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

// Test encodeTrailers when content type didn't match (should not finalize).
TEST_F(AwsEventstreamParserFilterTest, TrailersSkippedWhenContentTypeMismatched) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // No fallback should be applied since content type didn't match
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

// Test encodeTrailers when processing is already complete (should not double-finalize).
TEST_F(AwsEventstreamParserFilterTest, TrailersSkippedWhenProcessingComplete) {
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

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  // Trailers should not trigger another finalize
  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  // Still only 1 metadata_added (no double finalize)
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

// Test preserve_existing when namespace exists but with a different key (should write).
TEST_F(AwsEventstreamParserFilterTest, PreserveExistingDifferentKey) {
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
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.preserved_existing_metadata"));
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
  // Original key should still exist
  EXPECT_EQ(999, metadata.fields().at("other_key").number_value());
}

// Test end_stream=true finalizes rules with on_missing fallback.
TEST_F(AwsEventstreamParserFilterTest, EndStreamFinalizesFallback) {
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
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

// Test finalizeRules with no deferred actions (all rules matched).
TEST_F(AwsEventstreamParserFilterTest, FinalizeWithNoDeferredActions) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send message that matches the rule
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Rule matched, so finalization at end_stream should produce no deferred actions
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
}

// Test ContentType with leading/trailing whitespace.
TEST_F(AwsEventstreamParserFilterTest, ContentTypeWithWhitespace) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"},
      {"content-type", "  application/vnd.amazon.eventstream  ; charset=utf-8"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.mismatched_content_type"));
}

// Test that encodeData with non-matching content type still returns Continue.
TEST_F(AwsEventstreamParserFilterTest, EncodeDataSkipsWhenContentTypeMismatched) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "text/plain"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // No processing should happen
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.empty_payload"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.parse_error"));
}

// Test multiple deferred fallback actions at end of stream.
TEST_F(AwsEventstreamParserFilterTest, MultipleDeferredFallbackActions) {
  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
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
  EXPECT_EQ(2, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
  EXPECT_EQ(2, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(0, metadata.fields().at("tokens").number_value());
  EXPECT_EQ(99, metadata.fields().at("input_tokens").number_value());
}

// Test on_missing without a value field does not write metadata (covers !action.value.has_value()).
TEST_F(AwsEventstreamParserFilterTest, OnMissingWithoutValueDoesNotWriteMetadata) {
  envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser proto_config;
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
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

// ===== Header Rule Tests =====

// Test basic string header extraction to metadata.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleStringExtraction) {
  const std::string config_with_header_rule = R"EOF(
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
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
  )EOF";

  setupFilter(config_with_header_rule);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildStringHeader(":event-type", "ContentBlockDelta");
  Buffer::OwnedImpl data(
      buildEventstreamMessageWithHeaders(es_headers, R"({"usage": {"total_tokens": 42}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Both payload and header metadata should be written
  EXPECT_EQ(2, findCounter("aws_eventstream_parser.resp.json.metadata_added"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("ContentBlockDelta", metadata.fields().at("event_type").string_value());
  EXPECT_EQ(42, metadata.fields().at("tokens").number_value());
}

// Test int32 header extraction.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleInt32Extraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "status-code"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "status"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildInt32Header("status-code", 200);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(200, metadata.fields().at("status").number_value());
}

// Test bool header extraction.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleBoolExtraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "is-final"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "final"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildBoolHeader("is-final", true);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_TRUE(metadata.fields().at("final").bool_value());
}

// Test header on_missing fallback at end of stream.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleOnMissingFallback) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_missing:
          metadata_namespace: "envoy.lb"
          key: "event_type"
          value: "unknown"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Message with no headers — header rule should not match
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"text": "hello"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("unknown", metadata.fields().at("event_type").string_value());

  // Fallback counter: 2 (content parser dummy on_missing + header rule on_missing)
  EXPECT_EQ(2, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
}

// Test header on_present with override value.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleOnPresentOverrideValue) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "has_event_type"
          value: true
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildStringHeader(":event-type", "SomeEvent");
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Override value (true) should be used instead of the actual header value ("SomeEvent")
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_TRUE(metadata.fields().at("has_event_type").bool_value());
}

// Test header rule stop_processing_after_matches: first match wins, second is skipped.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleStopProcessingAfterMatches) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
        stop_processing_after_matches: 1
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // First message with matching header
  std::string es_headers1 = buildStringHeader(":event-type", "FirstEvent");
  Buffer::OwnedImpl data1(buildEventstreamMessageWithHeaders(es_headers1, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // Second message with same header but different value (should be skipped)
  std::string es_headers2 = buildStringHeader(":event-type", "SecondEvent");
  Buffer::OwnedImpl data2(buildEventstreamMessageWithHeaders(es_headers2, R"({"text": "bye"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  // First value should win
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("FirstEvent", metadata.fields().at("event_type").string_value());
}

// Test header rule default behavior (stop_processing_after_matches: 0): later matches overwrite.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleDefaultOverwritesBehavior) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // First message
  std::string es_headers1 = buildStringHeader(":event-type", "FirstEvent");
  Buffer::OwnedImpl data1(buildEventstreamMessageWithHeaders(es_headers1, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // Second message overwrites the first
  std::string es_headers2 = buildStringHeader(":event-type", "SecondEvent");
  Buffer::OwnedImpl data2(buildEventstreamMessageWithHeaders(es_headers2, R"({"text": "bye"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  // Second value should win (last-write-wins)
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("SecondEvent", metadata.fields().at("event_type").string_value());
}

// Test header rule stop_processing_after_matches with on_missing: fires when header never appears.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleStopProcessingOnMissingStillFires) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
        on_missing:
          metadata_namespace: "envoy.lb"
          key: "event_type"
          value: "unknown"
        stop_processing_after_matches: 1
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Message with no matching header
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"text": "hello"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // on_missing should fire at finalization
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("unknown", metadata.fields().at("event_type").string_value());
  EXPECT_EQ(2, findCounter("aws_eventstream_parser.resp.json.metadata_from_fallback"));
}

// Test no header_rules configured — backward compatible.
TEST_F(AwsEventstreamParserFilterTest, NoHeaderRulesBackwardCompatible) {
  // basic_config_ has no header_rules — existing behavior should be unchanged.
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildStringHeader(":event-type", "SomeEvent");
  Buffer::OwnedImpl data(
      buildEventstreamMessageWithHeaders(es_headers, R"({"usage": {"total_tokens": 100}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Payload rule should still work; no header metadata expected.
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
  EXPECT_FALSE(metadata.fields().contains("event_type"));
}

// Test header in a later message still matches.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleMatchesInLaterMessage) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "event_type"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // First message: no matching header
  Buffer::OwnedImpl data1(buildEventstreamMessage(R"({"text": "hello"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));

  // Second message: has the matching header
  std::string es_headers = buildStringHeader(":event-type", "MessageStop");
  Buffer::OwnedImpl data2(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "bye"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("MessageStop", metadata.fields().at("event_type").string_value());
}

// Test header rule uses default namespace when none specified.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleDefaultNamespace) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: ":event-type"
        on_present:
          key: "event_type"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildStringHeader(":event-type", "ContentBlockDelta");
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // Should use the default namespace
  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at(
      "envoy.filters.http.aws_eventstream_parser");
  EXPECT_EQ("ContentBlockDelta", metadata.fields().at("event_type").string_value());
}

// Test that string-to-number conversion failure produces KIND_NOT_SET and does not write metadata.
TEST_F(AwsEventstreamParserFilterTest, StringToNumberConversionFailureDoesNotWriteMetadata) {
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
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

// Test byte header extraction.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleByteExtraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "priority"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "priority"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildByteHeader("priority", 42);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(42, metadata.fields().at("priority").number_value());
}

// Test short header extraction.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleShortExtraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "port"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "port"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildShortHeader("port", 8080);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(8080, metadata.fields().at("port").number_value());
}

// Test int64 header extraction.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleInt64Extraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "request-id"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "request_id"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildInt64Header("request-id", 1234567890123LL);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(1234567890123.0, metadata.fields().at("request_id").number_value());
}

// Test timestamp header extraction.
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleTimestampExtraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "timestamp"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "ts"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string es_headers = buildTimestampHeader("timestamp", 1700000000000LL);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(1700000000000.0, metadata.fields().at("ts").number_value());
}

// Test byte array header extraction (hex-encoded).
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleByteArrayExtraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "checksum"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "checksum"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::string bytes = {'\xDE', '\xAD', '\xBE', '\xEF'};
  std::string es_headers = buildByteArrayHeader("checksum", bytes);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("deadbeef", metadata.fields().at("checksum").string_value());
}

// Test UUID header extraction (formatted as xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
TEST_F(AwsEventstreamParserFilterTest, HeaderRuleUuidExtraction) {
  const std::string config = R"EOF(
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "dummy"
              on_missing:
                metadata_namespace: "envoy.lb"
                key: "dummy"
                value:
                  string_value: "x"
    header_rules:
      - header_name: "trace-id"
        on_present:
          metadata_namespace: "envoy.lb"
          key: "trace_id"
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  std::array<uint8_t, 16> uuid = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
  std::string es_headers = buildUuidHeader("trace-id", uuid);
  Buffer::OwnedImpl data(buildEventstreamMessageWithHeaders(es_headers, R"({"text": "hi"})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ("01234567-89ab-cdef-fedc-ba9876543210",
            metadata.fields().at("trace_id").string_value());
}

// Test preserve_existing within the same pending batch (two messages writing same key).
TEST_F(AwsEventstreamParserFilterTest, PreserveExistingInPendingBatch) {
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
                preserve_existing_metadata_value: true
  )EOF";

  setupFilter(config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Two messages in the same buffer, both matching the same rule with preserve_existing.
  std::string msg1 = buildEventstreamMessage(R"({"usage": {"total_tokens": 100}})");
  std::string msg2 = buildEventstreamMessage(R"({"usage": {"total_tokens": 200}})");

  Buffer::OwnedImpl data(msg1 + msg2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  // First value should win; second should be preserved away.
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.preserved_existing_metadata"));

  const auto& metadata = stream_info_.dynamicMetadata().filter_metadata().at("envoy.lb");
  EXPECT_EQ(100, metadata.fields().at("tokens").number_value());
}

TEST_F(AwsEventstreamParserFilterTest, TypeConversionError) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/vnd.amazon.eventstream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  // Send a string value where NUMBER type is expected; "not_a_number" cannot be parsed as a double.
  Buffer::OwnedImpl data(buildEventstreamMessage(R"({"usage": {"total_tokens": "not_a_number"}})"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));

  EXPECT_EQ(1, findCounter("aws_eventstream_parser.resp.json.type_conversion_error"));
  EXPECT_EQ(0, findCounter("aws_eventstream_parser.resp.json.metadata_added"));
}

} // namespace
} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
