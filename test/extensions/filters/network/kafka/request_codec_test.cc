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

template <typename T> std::shared_ptr<T> RequestDecoderTest::serializeAndDeserialize(T request) {
  RequestEncoder serializer{buffer_};
  serializer.encode(request);

  std::shared_ptr<MockMessageListener> mock_listener = std::make_shared<MockMessageListener>();
  RequestDecoder testee{RequestParserResolver::KAFKA_0_11, {mock_listener}};

  MessageSharedPtr receivedMessage;
  EXPECT_CALL(*mock_listener, onMessage(_)).WillOnce(testing::SaveArg<0>(&receivedMessage));

  testee.onData(buffer_);

  return std::dynamic_pointer_cast<T>(receivedMessage);
};

// === OFFSET COMMIT (8) =======================================================

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV0) {
  // given
  NULLABLE_ARRAY<OffsetCommitTopic> topics{{{"topic1", {{{{0, 10, "m1"}}}}}}};
  OffsetCommitRequest request{"group_id", topics};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV1) {
  // given
  // partitions have timestamp in v1 only
  NULLABLE_ARRAY<OffsetCommitTopic> topics{
      {{"topic1", {{{0, 10, 100, "m1"}, {2, 20, 101, "m2"}}}}, {"topic2", {{{3, 30, 102, "m3"}}}}}};
  OffsetCommitRequest request{"group_id",
                              40,          // group_generation_id
                              "member_id", // member_id
                              topics};
  request.apiVersion() = 1;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === UNKNOWN REQUEST =========================================================

TEST_F(RequestDecoderTest, shouldProduceAbortedMessageOnUnknownData) {
  // given
  RequestEncoder serializer{buffer_};
  NULLABLE_ARRAY<OffsetCommitTopic> topics{{{"topic1", {{{{0, 10, "m1"}}}}}}};
  OffsetCommitRequest request{"group_id", topics};
  request.apiVersion() = 1;
  request.correlationId() = 42;
  request.clientId() = "client-id";

  serializer.encode(request);

  std::shared_ptr<MockMessageListener> mock_listener = std::make_shared<MockMessageListener>();
  RequestParserResolver parser_resolver{{}}; // we do not accept any kind of message here
  RequestDecoder testee{parser_resolver, {mock_listener}};

  MessageSharedPtr rev;
  EXPECT_CALL(*mock_listener, onMessage(_)).WillOnce(testing::SaveArg<0>(&rev));

  // when
  testee.onData(buffer_);

  // then
  auto received = std::dynamic_pointer_cast<UnknownRequest>(rev);
  ASSERT_NE(received, nullptr);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
