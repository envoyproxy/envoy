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

} // namespace
} // namespace AwsEventstreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
