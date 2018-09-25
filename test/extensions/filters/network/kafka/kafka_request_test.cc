#include "extensions/filters/network/kafka/kafka_request.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

TEST(RequestParserResolver, ShouldReturnSentinelIfRequestTypeIsNotRegistered) {
  // given
  RequestParserResolver testee{{}};
  RequestContextSharedPtr context{new RequestContext{}};

  // when
  ParserSharedPtr result = testee.createParser(0, 1, context); // api_key = 0 was not registered

  // then
  ASSERT_NE(result, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<SentinelConsumer>(result), nullptr);
}

TEST(RequestParserResolver, ShouldReturnSentinelIfRequestVersionIsNotRegistered) {
  // given
  GeneratorFunction generator = [](RequestContextSharedPtr arg) -> ParserSharedPtr {
    return std::make_shared<FatProduceRequestV0Parser>(arg);
  };
  RequestParserResolver testee{{{0, {0, 1}, generator}}};
  RequestContextSharedPtr context{new RequestContext{}};

  // when
  ParserSharedPtr result =
      testee.createParser(0, 2, context); // api_version = 2 was not registered (0 & 1 were)

  // then
  ASSERT_NE(result, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<SentinelConsumer>(result), nullptr);
}

TEST(RequestParserResolver, ShouldInvokeGeneratorFunctionOnMatch) {
  // given
  GeneratorFunction generator = [](RequestContextSharedPtr arg) -> ParserSharedPtr {
    return std::make_shared<FatProduceRequestV0Parser>(arg);
  };
  RequestParserResolver testee{{{0, {0, 1, 2, 3}, generator}}};
  RequestContextSharedPtr context{new RequestContext{}};

  // when
  ParserSharedPtr result = testee.createParser(0, 3, context);

  // then
  ASSERT_NE(result, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<FatProduceRequestV0Parser>(result), nullptr);
}

class BufferBasedTest : public testing::Test {
public:
  Buffer::OwnedImpl& buffer() { return buffer_; }

  const char* getBytes() {
    uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
    Buffer::RawSlice slices[num_slices];
    buffer_.getRawSlices(slices, num_slices);
    return reinterpret_cast<const char*>((slices[0]).mem_);
  }

private:
  Buffer::OwnedImpl buffer_;
};

EncodingContext encoder{-1};

TEST_F(BufferBasedTest, RequestStartParserTestShouldReturnRequestHeaderParser) {
  // given
  RequestStartParser testee{RequestParserResolver{{}}};

  INT32 request_len = 1234;
  encoder.encode(request_len, buffer());

  const char* bytes = getBytes();
  uint64_t remaining = 1024;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_NE(std::dynamic_pointer_cast<RequestHeaderParser>(result.next_parser_), nullptr);
  ASSERT_EQ(result.message_, nullptr);
  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len);
}

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver() : RequestParserResolver{{}} {};
  MOCK_CONST_METHOD3(createParser, ParserSharedPtr(INT16, INT16, RequestContextSharedPtr));
};

TEST_F(BufferBasedTest, RequestHeaderParserShouldExtractHeaderDataAndResolveNextParser) {
  // given
  const MockRequestParserResolver parser_resolver;
  const ParserSharedPtr parser{new ApiVersionsRequestV0Parser{nullptr}};
  EXPECT_CALL(parser_resolver, createParser(_, _, _)).WillOnce(Return(parser));

  const INT32 request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  RequestHeaderParser testee{parser_resolver, context};

  const INT16 api_key{1};
  const INT16 api_version{2};
  const INT32 correlation_id{10};
  const NULLABLE_STRING client_id{"aaa"};
  size_t written = 0;
  written += encoder.encode(api_key, buffer());
  written += encoder.encode(api_version, buffer());
  written += encoder.encode(correlation_id, buffer());
  written += encoder.encode(client_id, buffer());

  const char* bytes = getBytes();
  uint64_t remaining = 100000;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, parser);
  ASSERT_EQ(result.message_, nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, request_len - written);
  ASSERT_EQ(remaining, orig_remaining - written);

  const RequestHeader expected_header{api_key, api_version, correlation_id, client_id};
  ASSERT_EQ(testee.contextForTest()->request_header_, expected_header);
}

TEST_F(BufferBasedTest, SentinelConsumerShouldConsumeDataUntilEndOfRequest) {
  // given
  const INT32 request_len = 1000;
  RequestContextSharedPtr context{new RequestContext()};
  context->remaining_request_size_ = request_len;
  SentinelConsumer testee{context};

  const BYTES garbage(request_len * 2);
  encoder.encode(garbage, buffer());

  const char* bytes = getBytes();
  uint64_t remaining = request_len * 2;
  const uint64_t orig_remaining = remaining;

  // when
  const ParseResponse result = testee.parse(bytes, remaining);

  // then
  ASSERT_EQ(result.hasData(), true);
  ASSERT_EQ(result.next_parser_, nullptr);
  ASSERT_NE(std::dynamic_pointer_cast<UnknownRequest>(result.message_), nullptr);

  ASSERT_EQ(testee.contextForTest()->remaining_request_size_, 0);
  ASSERT_EQ(remaining, orig_remaining - request_len);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
