#include "extensions/filters/network/kafka/kafka_request_parser.h"

#include "test/extensions/filters/network/kafka/serialization_utilities.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

const int32_t FAILED_DESERIALIZER_STEP = 13;

class BufferBasedTest : public testing::Test {
public:
  Buffer::OwnedImpl& buffer() { return buffer_; }

  const char* getBytes() {
    uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
    STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
    buffer_.getRawSlices(slices.begin(), num_slices);
    return reinterpret_cast<const char*>((slices[0]).mem_);
  }

protected:
  Buffer::OwnedImpl buffer_;
  EncodingContext encoder_{-1}; // api_version is not used for request header
};

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver(){};
  MOCK_CONST_METHOD3(createParser, ParserSharedPtr(int16_t, int16_t, RequestContextSharedPtr));
};

TEST_F(BufferBasedTest, RequestStartParserTestShouldReturnRequestHeaderParser) {
  // given
  MockRequestParserResolver resolver{};
  RequestStartParser testee{resolver};

  int32_t request_len = 1234;
  encoder_.encode(request_len, buffer());

  const absl::string_view orig_data = {getBytes(), 1024};
  absl::string_view data = orig_data;

  // when
  const ParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<RequestHeaderParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len);
  assertStringViewIncrement(data, orig_data, sizeof(int32_t));
}

class MockParser : public Parser {
public:
  ParseResponse parse(absl::string_view&) override {
    throw new EnvoyException("should not be invoked");
  }
};

TEST_F(BufferBasedTest, RequestHeaderParserShouldExtractHeaderDataAndResolveNextParser) {
  // given
  const MockRequestParserResolver parser_resolver;
  const ParserSharedPtr parser{new MockParser{}};
  EXPECT_CALL(parser_resolver, createParser(_, _, _)).WillOnce(Return(parser));

  const int32_t request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  RequestHeaderParser testee{parser_resolver, context};

  const int16_t api_key{1};
  const int16_t api_version{2};
  const int32_t correlation_id{10};
  const NullableString client_id{"aaa"};
  size_t header_len = 0;
  header_len += encoder_.encode(api_key, buffer());
  header_len += encoder_.encode(api_version, buffer());
  header_len += encoder_.encode(correlation_id, buffer());
  header_len += encoder_.encode(client_id, buffer());

  const absl::string_view orig_data = {getBytes(), 100000};
  absl::string_view data = orig_data;

  // when
  const ParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, parser);
  ASSERT_EQ(result.message_, nullptr);

  const RequestHeader expected_header{api_key, api_version, correlation_id, client_id};
  ASSERT_EQ(testee.contextForTest()->request_header_, expected_header);
  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len - header_len);

  assertStringViewIncrement(data, orig_data, header_len);
}

TEST_F(BufferBasedTest, RequestHeaderParserShouldHandleDeserializerExceptionsDuringFeeding) {
  // given

  // throws during feeding
  class ThrowingRequestHeaderDeserializer : public RequestHeaderDeserializer {
  public:
    size_t feed(absl::string_view& data) override {
      // move some pointers to simulate data consumption
      data = {data.data() + FAILED_DESERIALIZER_STEP, data.size() - FAILED_DESERIALIZER_STEP};
      throw EnvoyException("feed");
    };

    bool ready() const override { throw std::runtime_error("should not be invoked at all"); };

    RequestHeader get() const override {
      throw std::runtime_error("should not be invoked at all");
    };
  };

  const MockRequestParserResolver parser_resolver;

  const int32_t request_size = 1024; // there are still 1024 bytes to read to complete the request
  RequestContextSharedPtr request_context{new RequestContext{request_size, {}}};
  RequestHeaderParser testee{parser_resolver, request_context,
                             std::make_unique<ThrowingRequestHeaderDeserializer>()};

  const absl::string_view orig_data = {getBytes(), 100000};
  absl::string_view data = orig_data;

  // when
  const ParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<SentinelParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_,
            request_size - FAILED_DESERIALIZER_STEP);

  assertStringViewIncrement(data, orig_data, FAILED_DESERIALIZER_STEP);
}

TEST_F(BufferBasedTest, RequestParserShouldHandleDeserializerExceptionsDuringFeeding) {
  // given
  // throws during feeding
  class ThrowingDeserializer : public Deserializer<int32_t> {
  public:
    size_t feed(absl::string_view&) override {
      // move some pointers to simulate data consumption
      throw EnvoyException("feed");
    };

    bool ready() const override { throw std::runtime_error("should not be invoked at all"); };

    int32_t get() const override { throw std::runtime_error("should not be invoked at all"); };
  };

  RequestContextSharedPtr request_context{new RequestContext{1024, {}}};
  RequestParser<int32_t, ThrowingDeserializer> testee{request_context};

  absl::string_view data = {getBytes(), 100000};

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

// deserializer that consumes FAILED_DESERIALIZER_STEP bytes and returns 0
class SomeBytesDeserializer : public Deserializer<int32_t> {
public:
  size_t feed(absl::string_view& data) override {
    data = {data.data() + FAILED_DESERIALIZER_STEP, data.size() - FAILED_DESERIALIZER_STEP};
    return FAILED_DESERIALIZER_STEP;
  };

  bool ready() const override { return true; };

  int32_t get() const override { return 0; };
};

TEST_F(BufferBasedTest, RequestParserShouldHandleDeserializerClaimingItsReadyButLeavingData) {
  // given
  const int32_t request_size = 1024; // there are still 1024 bytes to read to complete the request
  RequestContextSharedPtr request_context{new RequestContext{request_size, {}}};

  RequestParser<int32_t, SomeBytesDeserializer> testee{request_context};

  const absl::string_view orig_data = {getBytes(), 100000};
  absl::string_view data = orig_data;

  // when
  const ParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<SentinelParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_,
            request_size - FAILED_DESERIALIZER_STEP);

  assertStringViewIncrement(data, orig_data, FAILED_DESERIALIZER_STEP);
}

TEST_F(BufferBasedTest, SentinelParserShouldConsumeDataUntilEndOfRequest) {
  // given
  const int32_t request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  SentinelParser testee{context};

  const Bytes garbage(request_len * 2);
  encoder_.encode(garbage, buffer());

  const absl::string_view orig_data = {getBytes(), request_len * 2};
  absl::string_view data = orig_data;

  // when
  const ParseResponse result = testee.parse(data);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<UnknownRequest>(result.message_), nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, 0);

  assertStringViewIncrement(data, orig_data, request_len);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
