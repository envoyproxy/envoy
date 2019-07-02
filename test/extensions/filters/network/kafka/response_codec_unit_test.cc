#include "extensions/filters/network/kafka/response_codec.h"

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
namespace ResponseCodecUnitTest {

class MockResponseInitialParserFactory : public ResponseInitialParserFactory {
public:
  MOCK_METHOD1(create, ResponseParserSharedPtr(const ResponseParserResolver&));
};

using MockResponseInitialParserFactorySharedPtr = std::shared_ptr<MockResponseInitialParserFactory>;

class MockParser : public ResponseParser {
public:
  MOCK_METHOD1(parse, ResponseParseResponse(absl::string_view&));
};

using MockParserSharedPtr = std::shared_ptr<MockParser>;

class MockResponseParserResolver : public ResponseParserResolver {
public:
  MockResponseParserResolver() : ResponseParserResolver({}){};
  MOCK_CONST_METHOD1(createParser, ResponseParserSharedPtr(ResponseContextSharedPtr));
};

class MockResponseCallback : public ResponseCallback {
public:
  MOCK_METHOD1(onMessage, void(AbstractResponseSharedPtr));
  MOCK_METHOD1(onFailedParse, void(ResponseMetadataSharedPtr));
};

using MockResponseCallbackSharedPtr = std::shared_ptr<MockResponseCallback>;

class ResponseCodecUnitTest : public testing::Test, public BufferBasedTest {
protected:
  MockResponseInitialParserFactorySharedPtr factory_{
      std::make_shared<MockResponseInitialParserFactory>()};
  MockResponseParserResolver parser_resolver_{};
  MockResponseCallbackSharedPtr callback_{std::make_shared<MockResponseCallback>()};
};

ResponseParseResponse consumeOneByte(absl::string_view& data) {
  data = {data.data() + 1, data.size() - 1};
  return ResponseParseResponse::stillWaiting();
}

TEST_F(ResponseCodecUnitTest, shouldDoNothingIfParserReturnsWaiting) {
  // given
  putGarbageIntoBuffer();

  MockParserSharedPtr parser = std::make_shared<MockParser>();
  EXPECT_CALL(*parser, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(parser));
  EXPECT_CALL(parser_resolver_, createParser(_)).Times(0);

  EXPECT_CALL(*callback_, onMessage(_)).Times(0);
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  ResponseDecoder testee{factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There were no interactions with `callback_`.
}

TEST_F(ResponseCodecUnitTest, shouldUseNewParserAsResponse) {
  // given
  putGarbageIntoBuffer();

  MockParserSharedPtr parser1 = std::make_shared<MockParser>();
  MockParserSharedPtr parser2 = std::make_shared<MockParser>();
  MockParserSharedPtr parser3 = std::make_shared<MockParser>();
  EXPECT_CALL(*parser1, parse(_)).WillOnce(Return(ResponseParseResponse::nextParser(parser2)));
  EXPECT_CALL(*parser2, parse(_)).WillOnce(Return(ResponseParseResponse::nextParser(parser3)));
  EXPECT_CALL(*parser3, parse(_)).Times(AnyNumber()).WillRepeatedly(Invoke(consumeOneByte));

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(parser1));
  EXPECT_CALL(parser_resolver_, createParser(_)).Times(0);

  EXPECT_CALL(*callback_, onMessage(_)).Times(0);
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  ResponseDecoder testee{factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  // There were no interactions with `callback_`.
}

TEST_F(ResponseCodecUnitTest, shouldPassParsedMessageToCallback) {
  // given
  putGarbageIntoBuffer();

  const AbstractResponseSharedPtr parsed_message =
      std::make_shared<Response<int32_t>>(ResponseMetadata{0, 0, 0}, 0);

  MockParserSharedPtr parser = std::make_shared<MockParser>();
  auto consume_and_return = [&parsed_message](absl::string_view& data) -> ResponseParseResponse {
    data = {data.data() + data.size(), 0};
    return ResponseParseResponse::parsedMessage(parsed_message);
  };
  EXPECT_CALL(*parser, parse(_)).WillOnce(Invoke(consume_and_return));

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(parser));
  EXPECT_CALL(parser_resolver_, createParser(_)).Times(0);

  EXPECT_CALL(*callback_, onMessage(parsed_message));
  EXPECT_CALL(*callback_, onFailedParse(_)).Times(0);

  ResponseDecoder testee{factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  // `callback_` had `onMessage` invoked once with matching argument.
}

TEST_F(ResponseCodecUnitTest, shouldPassParseFailureDataToCallback) {
  // given
  putGarbageIntoBuffer();

  const ResponseMetadataSharedPtr failure_data = std::make_shared<ResponseMetadata>(0, 0, 0);

  MockParserSharedPtr parser = std::make_shared<MockParser>();
  auto consume_and_return = [&failure_data](absl::string_view& data) -> ResponseParseResponse {
    data = {data.data() + data.size(), 0};
    return ResponseParseResponse::parseFailure(failure_data);
  };
  EXPECT_CALL(*parser, parse(_)).WillOnce(Invoke(consume_and_return));

  EXPECT_CALL(*factory_, create(_)).WillOnce(Return(parser));
  EXPECT_CALL(parser_resolver_, createParser(_)).Times(0);

  EXPECT_CALL(*callback_, onMessage(_)).Times(0);
  EXPECT_CALL(*callback_, onFailedParse(failure_data));

  ResponseDecoder testee{factory_, parser_resolver_, {callback_}};

  // when
  testee.onData(buffer_);

  // then
  // `callback_` had `onFailedParse` invoked once with matching argument.
}

} // namespace ResponseCodecUnitTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
