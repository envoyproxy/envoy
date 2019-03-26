#include "extensions/filters/network/kafka/request_codec.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::Invoke;
using testing::ResultOf;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class MockParserFactory : public InitialParserFactory {
public:
  MOCK_CONST_METHOD1(create, ParserSharedPtr(const RequestParserResolver&));
};

class MockParser : public Parser {
public:
  MOCK_METHOD1(parse, ParseResponse(absl::string_view&));
};

typedef std::shared_ptr<MockParser> MockParserSharedPtr;

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver() : RequestParserResolver({}){};
  MOCK_CONST_METHOD3(createParser, ParserSharedPtr(int16_t, int16_t, RequestContextSharedPtr));
};

class MockRequestCallback : public RequestCallback {
public:
  MOCK_METHOD1(onMessage, void(MessageSharedPtr));
};

typedef std::shared_ptr<MockRequestCallback> MockRequestCallbackSharedPtr;

class RequestCodecUnitTest : public testing::Test {
protected:
  template <typename T> void putInBuffer(T arg);

  Buffer::OwnedImpl buffer_;

  MockParserFactory initial_parser_factory_{};
  MockRequestParserResolver parser_resolver_{};
  MockRequestCallbackSharedPtr request_callback_{std::make_shared<MockRequestCallback>()};
};

ParseResponse consumeOneByte(absl::string_view& data) {
  data = {data.data() + 1, data.size() - 1};
  return ParseResponse::stillWaiting();
}

TEST_F(RequestCodecUnitTest, shouldDoNothingIfParserNeverReturnsMessage) {
  // given
  putInBuffer(ConcreteRequest<int32_t>{{}, 0});

  MockParserSharedPtr parser = std::make_shared<MockParser>();
  EXPECT_CALL(*parser, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(initial_parser_factory_, create(_)).WillOnce(Return(parser));

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {request_callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There were no interactions with `request_callback`.
}

TEST_F(RequestCodecUnitTest, shouldUseNewParserAsResponse) {
  // given
  putInBuffer(ConcreteRequest<int32_t>{{}, 0});

  MockParserSharedPtr parser1 = std::make_shared<MockParser>();
  MockParserSharedPtr parser2 = std::make_shared<MockParser>();
  MockParserSharedPtr parser3 = std::make_shared<MockParser>();
  EXPECT_CALL(*parser1, parse(_)).WillOnce(Return(ParseResponse::nextParser(parser2)));
  EXPECT_CALL(*parser2, parse(_)).WillOnce(Return(ParseResponse::nextParser(parser3)));
  EXPECT_CALL(*parser3, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(initial_parser_factory_, create(_)).WillOnce(Return(parser1));

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {request_callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There were no interactions with `request_callback`.
}

TEST_F(RequestCodecUnitTest, shouldReturnParsedMessageAndReinitialize) {
  // given
  putInBuffer(ConcreteRequest<int32_t>{{}, 0});

  MockParserSharedPtr parser1 = std::make_shared<MockParser>();
  MessageSharedPtr message = std::make_shared<UnknownRequest>(RequestHeader{});
  EXPECT_CALL(*parser1, parse(_)).WillOnce(Return(ParseResponse::parsedMessage(message)));

  MockParserSharedPtr parser2 = std::make_shared<MockParser>();
  EXPECT_CALL(*parser2, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(initial_parser_factory_, create(_))
      .WillOnce(Return(parser1))
      .WillOnce(Return(parser2));

  EXPECT_CALL(*request_callback_, onMessage(message));

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {request_callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There was only one message sent to `request_callback`.
}

TEST_F(RequestCodecUnitTest, shouldInvokeParsersEvenIfTheyDoNotConsumeZeroBytes) {
  // given
  putInBuffer(ConcreteRequest<int32_t>{{}, 0});

  MockParserSharedPtr parser1 = std::make_shared<MockParser>();
  MockParserSharedPtr parser2 = std::make_shared<MockParser>();
  MockParserSharedPtr parser3 = std::make_shared<MockParser>();

  auto consume_and_return = [this, &parser2](absl::string_view& data) -> ParseResponse {
    data = {data.data() + buffer_.length(), data.size() - buffer_.length()};
    return ParseResponse::nextParser(parser2);
  };
  EXPECT_CALL(*parser1, parse(_)).WillOnce(Invoke(consume_and_return));
  MessageSharedPtr message = std::make_shared<UnknownRequest>(RequestHeader{});
  EXPECT_CALL(*parser2, parse(_)).WillOnce(Return(ParseResponse::parsedMessage(message)));
  EXPECT_CALL(*parser3, parse(ResultOf([](absl::string_view arg) { return arg.size(); }, Eq(0))))
      .WillOnce(Return(ParseResponse::stillWaiting()));

  EXPECT_CALL(initial_parser_factory_, create(_))
      .WillOnce(Return(parser1))
      .WillOnce(Return(parser3));

  EXPECT_CALL(*request_callback_, onMessage(message));

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {request_callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There was only one message sent to `request_callback`.
  // After that, `parser3` was created and passed remaining data (that should have been empty).
}

// Helper function.
template <typename T> void RequestCodecUnitTest::putInBuffer(T arg) {
  MessageEncoderImpl serializer{buffer_};
  serializer.encode(arg);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
