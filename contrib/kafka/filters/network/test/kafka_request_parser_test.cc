#include "contrib/kafka/filters/network/source/kafka_request_parser.h"
#include "contrib/kafka/filters/network/test/buffer_based_test.h"
#include "contrib/kafka/filters/network/test/serialization_utilities.h"
#include "gmock/gmock.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace KafkaRequestParserTest {

const int32_t FAILED_DESERIALIZER_STEP = 13;

class KafkaRequestParserTest : public testing::Test, public BufferBasedTest {};

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver() = default;
  MOCK_METHOD(RequestParserSharedPtr, createParser, (int16_t, int16_t, RequestContextSharedPtr),
              (const));
};

TEST_F(KafkaRequestParserTest, RequestStartParserTestShouldReturnRequestHeaderParser) {
  // given
  MockRequestParserResolver resolver{};
  RequestStartParser testee{resolver};

  int32_t request_len = 1234;
  putIntoBuffer(request_len);

  const absl::string_view orig_data = {getBytes(), 1024};
  absl::string_view data = orig_data;

  // when
  const RequestParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<RequestHeaderParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(result.failure_data_, nullptr);
  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len);
  assertStringViewIncrement(data, orig_data, sizeof(int32_t));
}

class MockParser : public RequestParser {
public:
  RequestParseResponse parse(absl::string_view&) override {
    throw EnvoyException("should not be invoked");
  }
};

TEST_F(KafkaRequestParserTest, RequestHeaderParserShouldExtractHeaderAndResolveNextParser) {
  // given
  const MockRequestParserResolver parser_resolver;
  const RequestParserSharedPtr parser{new MockParser{}};
  EXPECT_CALL(parser_resolver, createParser(_, _, _)).WillOnce(Return(parser));

  const int32_t request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  RequestHeaderParser testee{parser_resolver, context};

  const int16_t api_key{1};
  const int16_t api_version{2};
  const int32_t correlation_id{10};
  const NullableString client_id{"aaa"};
  uint32_t header_len = 0;
  header_len += putIntoBuffer(api_key);
  header_len += putIntoBuffer(api_version);
  header_len += putIntoBuffer(correlation_id);
  header_len += putIntoBuffer(client_id);

  const absl::string_view orig_data = putGarbageIntoBuffer();
  absl::string_view data = orig_data;

  // when
  const RequestParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, parser);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(result.failure_data_, nullptr);

  const RequestHeader expected_header{api_key, api_version, correlation_id, client_id};
  ASSERT_EQ(testee.contextForTest()->request_header_, expected_header);
  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len - header_len);

  assertStringViewIncrement(data, orig_data, header_len);
}

TEST_F(KafkaRequestParserTest, RequestDataParserShouldHandleDeserializerExceptionsDuringFeeding) {
  // given

  // This deserializer throws during feeding.
  class ThrowingDeserializer : public Deserializer<int32_t> {
  public:
    uint32_t feed(absl::string_view&) override {
      // Move some pointers to simulate data consumption.
      throw EnvoyException("feed");
    };

    bool ready() const override { throw std::runtime_error("should not be invoked at all"); };

    int32_t get() const override { throw std::runtime_error("should not be invoked at all"); };
  };

  RequestContextSharedPtr request_context{new RequestContext{1024, {0, 0, 0, absl::nullopt}}};
  RequestDataParser<int32_t, ThrowingDeserializer> testee{request_context};

  absl::string_view data = putGarbageIntoBuffer();

  // when
  bool caught = false;
  try {
    testee.parse(data);
  } catch (EnvoyException& e) {
    caught = true;
  }

  // then
  ASSERT_EQ(caught, true);
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

TEST_F(KafkaRequestParserTest,
       RequestDataParserShouldHandleDeserializerReturningReadyButLeavingData) {
  // given
  const int32_t request_size = 1024; // There are still 1024 bytes to read to complete the request.
  RequestContextSharedPtr request_context{
      new RequestContext{request_size, {0, 0, 0, absl::nullopt}}};

  RequestDataParser<int32_t, SomeBytesDeserializer> testee{request_context};

  const absl::string_view orig_data = putGarbageIntoBuffer();
  absl::string_view data = orig_data;

  // when
  const RequestParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<SentinelParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(result.failure_data_, nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_,
            request_size - FAILED_DESERIALIZER_STEP);

  assertStringViewIncrement(data, orig_data, FAILED_DESERIALIZER_STEP);
}

TEST_F(KafkaRequestParserTest, SentinelParserShouldConsumeDataUntilEndOfRequest) {
  // given
  const int32_t request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  SentinelParser testee{context};

  const absl::string_view orig_data = putGarbageIntoBuffer(request_len * 2);
  absl::string_view data = orig_data;

  // when
  const RequestParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<RequestParseFailure>(result.failure_data_), nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, 0);

  assertStringViewIncrement(data, orig_data, request_len);
}

} // namespace KafkaRequestParserTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
