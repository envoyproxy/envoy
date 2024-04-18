#include "test/test_common/utility.h"

#include "contrib/kafka/filters/network/source/kafka_response_parser.h"
#include "contrib/kafka/filters/network/test/buffer_based_test.h"
#include "contrib/kafka/filters/network/test/serialization_utilities.h"
#include "gmock/gmock.h"

using testing::_;
using testing::ContainsRegex;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace KafkaResponseParserTest {

const int32_t FAILED_DESERIALIZER_STEP = 13;

class KafkaResponseParserTest : public testing::Test, public BufferBasedTest {};

class MockResponseParserResolver : public ResponseParserResolver {
public:
  MockResponseParserResolver() = default;
  MOCK_METHOD(ResponseParserSharedPtr, createParser, (ResponseContextSharedPtr), (const));
};

class MockParser : public ResponseParser {
public:
  ResponseParseResponse parse(absl::string_view&) override {
    throw EnvoyException("should not be invoked");
  }
};

TEST_F(KafkaResponseParserTest, ResponseHeaderParserShouldExtractHeaderAndResolveNextParser) {
  // given
  const int32_t payload_length = 100;
  const int32_t correlation_id = 1234;
  uint32_t header_len = 0;
  header_len += putIntoBuffer(payload_length);
  header_len += putIntoBuffer(correlation_id); // Insert correlation id.

  const absl::string_view orig_data = putGarbageIntoBuffer();
  absl::string_view data = orig_data;

  const int16_t api_key = 42;
  const int16_t api_version = 123;

  ExpectedResponsesSharedPtr expected_responses = std::make_shared<ExpectedResponses>();
  (*expected_responses)[correlation_id] = {api_key, api_version};

  const MockResponseParserResolver parser_resolver;
  const ResponseParserSharedPtr parser{new MockParser{}};
  EXPECT_CALL(parser_resolver, createParser(_)).WillOnce(Return(parser));

  ResponseHeaderParser testee{expected_responses, parser_resolver};

  // when
  const ResponseParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, parser);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(result.failure_data_, nullptr);

  const auto context = testee.contextForTest();
  ASSERT_EQ(context->remaining_response_size_, payload_length - sizeof(correlation_id));
  ASSERT_EQ(context->correlation_id_, correlation_id);
  ASSERT_EQ(context->api_key_, api_key);
  ASSERT_EQ(context->api_version_, api_version);

  ASSERT_EQ(expected_responses->size(), 0);

  assertStringViewIncrement(data, orig_data, header_len);
}

TEST_F(KafkaResponseParserTest, ResponseHeaderParserShouldThrowIfThereIsUnexpectedResponse) {
  // given
  const int32_t payload_length = 100;
  const int32_t correlation_id = 1234;
  putIntoBuffer(payload_length);
  putIntoBuffer(correlation_id); // Insert correlation id.

  absl::string_view data = putGarbageIntoBuffer();

  ExpectedResponsesSharedPtr expected_responses = std::make_shared<ExpectedResponses>();
  const MockResponseParserResolver parser_resolver;
  const ResponseParserSharedPtr parser{new MockParser{}};
  EXPECT_CALL(parser_resolver, createParser(_)).Times(0);

  ResponseHeaderParser testee{expected_responses, parser_resolver};

  // when
  // then - exception gets thrown.
  EXPECT_THAT_THROWS_MESSAGE(testee.parse(data), EnvoyException,
                             AllOf(ContainsRegex("no response metadata registered"),
                                   ContainsRegex(std::to_string(correlation_id))));
}

TEST_F(KafkaResponseParserTest, ResponseDataParserShoulRethrowDeserializerExceptionsDuringFeeding) {
  // given

  // This deserializer throws during feeding.
  class ThrowingDeserializer : public Deserializer<int32_t> {
  public:
    uint32_t feed(absl::string_view&) override {
      // Move some pointers to simulate data consumption.
      throw EnvoyException("deserializer-failure");
    };

    bool ready() const override { throw std::runtime_error("should not be invoked at all"); };

    int32_t get() const override { throw std::runtime_error("should not be invoked at all"); };
  };

  ResponseContextSharedPtr context = std::make_shared<ResponseContext>();
  ResponseDataParser<int32_t, ThrowingDeserializer> testee{context};

  absl::string_view data = putGarbageIntoBuffer();

  // when
  // then - exception gets thrown.
  EXPECT_THROW_WITH_MESSAGE(testee.parse(data), EnvoyException, "deserializer-failure");
}

// This deserializer consumes FAILED_DESERIALIZER_STEP bytes and returns 0
class SomeBytesDeserializer : public Deserializer<int32_t> {
public:
  uint32_t feed(absl::string_view& data) override {
    data = {data.data() + FAILED_DESERIALIZER_STEP, data.size() - FAILED_DESERIALIZER_STEP};
    return FAILED_DESERIALIZER_STEP;
  };

  bool ready() const override { return true; };

  int32_t get() const override { return 0; };
};

TEST_F(KafkaResponseParserTest,
       ResponseDataParserShouldHandleDeserializerReturningReadyButLeavingData) {
  // given
  const int32_t message_size = 1024; // There are still 1024 bytes to read to complete the message.
  ResponseContextSharedPtr context = std::make_shared<ResponseContext>();
  context->remaining_response_size_ = message_size;

  ResponseDataParser<int32_t, SomeBytesDeserializer> testee{context};

  const absl::string_view orig_data = putGarbageIntoBuffer();
  absl::string_view data = orig_data;

  // when
  const ResponseParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<SentinelResponseParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(result.failure_data_, nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_response_size_,
            message_size - FAILED_DESERIALIZER_STEP);

  assertStringViewIncrement(data, orig_data, FAILED_DESERIALIZER_STEP);
}

TEST_F(KafkaResponseParserTest, SentinelResponseParserShouldConsumeDataUntilEndOfMessage) {
  // given
  const int32_t response_len = 1000;
  ResponseContextSharedPtr context = std::make_shared<ResponseContext>();
  context->remaining_response_size_ = response_len;
  SentinelResponseParser testee{context};

  const absl::string_view orig_data = putGarbageIntoBuffer(response_len * 2);
  absl::string_view data = orig_data;

  // when
  const ResponseParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<ResponseMetadata>(result.failure_data_), nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_response_size_, 0);

  assertStringViewIncrement(data, orig_data, response_len);
}

} // namespace KafkaResponseParserTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
