// DO NOT EDIT - THIS FILE WAS GENERATED
// clang-format off
#include "extensions/filters/network/kafka/generated/requests.h"
#include "extensions/filters/network/kafka/request_codec.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

template <typename T> std::shared_ptr<T> RequestDecoderTest::serializeAndDeserialize(T request) {
  RequestEncoder serializer{buffer_};
  serializer.encode(request);

  std::shared_ptr<MockMessageListener> mock_listener = std::make_shared<MockMessageListener>();
  RequestDecoder testee{RequestParserResolver::INSTANCE, {mock_listener}};

  MessageSharedPtr receivedMessage;
  EXPECT_CALL(*mock_listener, onMessage(testing::_)).WillOnce(testing::SaveArg<0>(&receivedMessage));

  testee.onData(buffer_);

  return std::dynamic_pointer_cast<T>(receivedMessage);
};

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV0) {
  // given
  OffsetCommitRequestV0 request = {"string", {{ {"string", {{ {32, 64, {"nullable"}, } }}, } }}, };

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV1) {
  // given
  OffsetCommitRequestV1 request = {"string", 32, "string", {{ {"string", {{ {32, 64, 64, {"nullable"}, } }}, } }}, };

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV2) {
  // given
  OffsetCommitRequestV2 request = {"string", 32, "string", 64, {{ {"string", {{ {32, 64, {"nullable"}, } }}, } }}, };

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV3) {
  // given
  OffsetCommitRequestV3 request = {"string", 32, "string", 64, {{ {"string", {{ {32, 64, {"nullable"}, } }}, } }}, };

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
// clang-format on
