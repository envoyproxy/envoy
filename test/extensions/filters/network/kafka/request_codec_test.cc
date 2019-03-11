#include "extensions/filters/network/kafka/request_codec.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class RequestDecoderTest : public testing::Test {
public:
  Buffer::OwnedImpl buffer_;

  template <typename T> std::shared_ptr<T> serializeAndDeserialize(T request);
};

class MockMessageListener : public RequestCallback {
public:
  MOCK_METHOD1(onMessage, void(MessageSharedPtr));
};

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver() : RequestParserResolver({}){};
  MOCK_CONST_METHOD3(createParser, ParserSharedPtr(int16_t, int16_t, RequestContextSharedPtr));
};

template <typename T> std::shared_ptr<T> RequestDecoderTest::serializeAndDeserialize(T request) {
  MessageEncoderImpl serializer{buffer_};
  serializer.encode(request);

  std::shared_ptr<MockMessageListener> mock_listener = std::make_shared<MockMessageListener>();
  RequestDecoder testee{RequestParserResolver::getDefaultInstance(), {mock_listener}};

  MessageSharedPtr receivedMessage;
  EXPECT_CALL(*mock_listener, onMessage(_)).WillOnce(testing::SaveArg<0>(&receivedMessage));

  testee.onData(buffer_);

  return std::dynamic_pointer_cast<T>(receivedMessage);
};

ParserSharedPtr createSentinelParser(testing::Unused, testing::Unused,
                                     RequestContextSharedPtr context) {
  return std::make_shared<SentinelParser>(context);
}

struct MockRequest {
  const int32_t field1_ = 1;
  const int64_t field2_ = 2;

  size_t encode(Buffer::Instance& buffer, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, buffer);
    written += encoder.encode(field2_, buffer);
    return written;
  }

  friend std::ostream& operator<<(std::ostream& os, const MockRequest&) {
    return os << "{MockRequest}";
  };
};

TEST_F(RequestDecoderTest, shouldProduceAbortedMessageOnUnknownData) {
  // given
  MessageEncoderImpl serializer{buffer_};
  MockRequest data{};
  // api key & version values do not matter, as resolver recognizes nothing
  ConcreteRequest<MockRequest> request = {{1000, 2000, 3000, "correlation-id"}, data};

  serializer.encode(request);

  MockRequestParserResolver mock_parser_resolver{};
  EXPECT_CALL(mock_parser_resolver, createParser(_, _, _))
      .WillOnce(testing::Invoke(createSentinelParser));
  std::shared_ptr<MockMessageListener> mock_listener = std::make_shared<MockMessageListener>();
  RequestDecoder testee{mock_parser_resolver, {mock_listener}};

  MessageSharedPtr rev;
  EXPECT_CALL(*mock_listener, onMessage(_)).WillOnce(testing::SaveArg<0>(&rev));

  // when
  testee.onData(buffer_);

  // then
  ASSERT_NE(rev, nullptr);
  auto received = std::dynamic_pointer_cast<UnknownRequest>(rev);
  ASSERT_NE(received, nullptr);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
