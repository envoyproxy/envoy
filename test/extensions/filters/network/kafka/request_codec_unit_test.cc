#include "extensions/filters/network/kafka/request_codec.h"

#include "test/extensions/filters/network/kafka/buffer_based_test.h"
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
namespace RequestCodecUnitTest {

class MockParserFactory : public InitialParserFactory {
public:
  MOCK_CONST_METHOD1(create, RequestParserSharedPtr(const RequestParserResolver&));
};

class MockParser : public RequestParser {
public:
  MOCK_METHOD1(parse, RequestParseResponse(absl::string_view&));
};

using MockParserSharedPtr = std::shared_ptr<MockParser>;

class MockRequestParserResolver : public RequestParserResolver {
public:
  MockRequestParserResolver() : RequestParserResolver({}){};
  MOCK_CONST_METHOD3(createParser,
                     RequestParserSharedPtr(int16_t, int16_t, RequestContextSharedPtr));
};

class MockRequestCallback : public RequestCallback {
public:
  MOCK_METHOD1(onMessage, void(AbstractRequestSharedPtr));
  MOCK_METHOD1(onFailedParse, void(RequestParseFailureSharedPtr));
};

using MockRequestCallbackSharedPtr = std::shared_ptr<MockRequestCallback>;

class RequestCodecUnitTest : public testing::Test, public BufferBasedTest {
protected:
  MockParserFactory initial_parser_factory_{};
  MockRequestParserResolver parser_resolver_{};
  MockRequestCallbackSharedPtr callback_{std::make_shared<MockRequestCallback>()};
};

RequestParseResponse consumeOneByte(absl::string_view& data) {
  data = {data.data() + 1, data.size() - 1};
  return RequestParseResponse::stillWaiting();
}

TEST_F(RequestCodecUnitTest, shouldDoNothingIfParserReturnsWaiting) {
  // given
  putGarbageIntoBuffer();

  MockParserSharedPtr parser = std::make_shared<MockParser>();
  EXPECT_CALL(*parser, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(initial_parser_factory_, create(_)).WillOnce(Return(parser));

  EXPECT_CALL(*callback_, onMessage(_)).Times(0);
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There were no interactions with `callback_`.
}

TEST_F(RequestCodecUnitTest, shouldUseNewParserAsResponse) {
  // given
  putGarbageIntoBuffer();

  MockParserSharedPtr parser1 = std::make_shared<MockParser>();
  MockParserSharedPtr parser2 = std::make_shared<MockParser>();
  MockParserSharedPtr parser3 = std::make_shared<MockParser>();
  EXPECT_CALL(*parser1, parse(_)).WillOnce(Return(RequestParseResponse::nextParser(parser2)));
  EXPECT_CALL(*parser2, parse(_)).WillOnce(Return(RequestParseResponse::nextParser(parser3)));
  EXPECT_CALL(*parser3, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(initial_parser_factory_, create(_)).WillOnce(Return(parser1));
  EXPECT_CALL(parser_resolver_, createParser(_, _, _)).Times(0);

  EXPECT_CALL(*callback_, onMessage(_)).Times(0);
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(testee.getCurrentParserForTest(), parser3);
  // Also, there were no interactions with `callback_`.
}

TEST_F(RequestCodecUnitTest, shouldPassParsedMessageToCallback) {
  // given
  putGarbageIntoBuffer();

  const AbstractRequestSharedPtr parsed_message =
      std::make_shared<Request<int32_t>>(RequestHeader{0, 0, 0, ""}, 0);

  MockParserSharedPtr all_consuming_parser = std::make_shared<MockParser>();
  auto consume_and_return = [&parsed_message](absl::string_view& data) -> RequestParseResponse {
    data = {data.data() + data.size(), 0};
    return RequestParseResponse::parsedMessage(parsed_message);
  };
  EXPECT_CALL(*all_consuming_parser, parse(_)).WillOnce(Invoke(consume_and_return));

  EXPECT_CALL(initial_parser_factory_, create(_)).WillOnce(Return(all_consuming_parser));
  EXPECT_CALL(parser_resolver_, createParser(_, _, _)).Times(0);

  EXPECT_CALL(*callback_, onMessage(parsed_message));
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(testee.getCurrentParserForTest(), nullptr);
  // Also, `callback_` had `onMessage` invoked once with matching argument.
}

TEST_F(RequestCodecUnitTest, shouldPassParsedMessageToCallbackAndInitializeNextParser) {
  // given
  putGarbageIntoBuffer();

  const AbstractRequestSharedPtr parsed_message =
      std::make_shared<Request<int32_t>>(RequestHeader(), 0);

  MockParserSharedPtr parser1 = std::make_shared<MockParser>();
  EXPECT_CALL(*parser1, parse(_))
      .WillOnce(Return(RequestParseResponse::parsedMessage(parsed_message)));

  MockParserSharedPtr parser2 = std::make_shared<MockParser>();
  EXPECT_CALL(*parser2, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(initial_parser_factory_, create(_))
      .WillOnce(Return(parser1))
      .WillOnce(Return(parser2));

  EXPECT_CALL(*callback_, onMessage(parsed_message));
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(testee.getCurrentParserForTest(), parser2);
  // Also, `callback_` had `onMessage` invoked once with matching argument.
}

TEST_F(RequestCodecUnitTest, shouldPassParseFailureDataToCallback) {
  // given
  putGarbageIntoBuffer();

  const RequestParseFailureSharedPtr failure_data =
      std::make_shared<RequestParseFailure>(RequestHeader());

  MockParserSharedPtr parser = std::make_shared<MockParser>();
  auto consume_and_return = [&failure_data](absl::string_view& data) -> RequestParseResponse {
    data = {data.data() + data.size(), 0};
    return RequestParseResponse::parseFailure(failure_data);
  };
  EXPECT_CALL(*parser, parse(_)).WillOnce(Invoke(consume_and_return));

  EXPECT_CALL(initial_parser_factory_, create(_)).WillOnce(Return(parser));
  EXPECT_CALL(parser_resolver_, createParser(_, _, _)).Times(0);

  EXPECT_CALL(*callback_, onMessage(_)).Times(0);
  EXPECT_CALL(*callback_, onFailedParse(failure_data));

  RequestDecoder testee{initial_parser_factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(testee.getCurrentParserForTest(), nullptr);
  // Also, `callback_` had `onFailedParse` invoked once with matching argument.
}

} // namespace RequestCodecUnitTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
