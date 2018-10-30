#include "extensions/filters/network/kafka/codec.h"

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

class MockMessageListener : public MessageListener {
public:
  MOCK_METHOD1(onMessage, void(MessageSharedPtr));
};

template <typename T> std::shared_ptr<T> RequestDecoderTest::serializeAndDeserialize(T request) {
  char data[1024];
  RequestSerializer serializer;
  size_t written = serializer.encode(request, data);
  buffer_.add(data, written);

  std::shared_ptr<MockMessageListener> mock_listener = std::make_shared<MockMessageListener>();
  RequestDecoder testee{RequestParserResolver::KAFKA_0_11, {mock_listener}};

  MessageSharedPtr receivedMessage;
  EXPECT_CALL(*mock_listener, onMessage(_)).WillOnce(testing::SaveArg<0>(&receivedMessage));

  testee.onData(buffer_);

  return std::dynamic_pointer_cast<T>(receivedMessage);
};

// === FETCH (1) ============================================================

TEST_F(RequestDecoderTest, shouldParseFetchRequest) {
  // given
  FetchRequest request{1,
                       1000,
                       10,
                       20,
                       2,
                       {{
                           {"topic1", {{{10, 20, 1000, 2000}}}},
                           {"topic1", {{{11, 21, 1001, 2001}, {12, 22, 1002, 2002}}}},
                           {"topic1", {{{13, 23, 1003, 2003}}}},
                       }}};
  request.apiVersion() = 5;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === LIST OFFSETS (2) ========================================================

TEST_F(RequestDecoderTest, shouldParseListOffsetsRequest) {
  // given
  ListOffsetsRequest request{10,
                             2,
                             {{
                                 {"topic1", {{{1, 1000}, {2, 2000}}}},
                                 {"topic2", {{{3, 3000}}}},
                             }}};
  request.apiVersion() = 2;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === METADATA (3) ============================================================

TEST_F(RequestDecoderTest, shouldParseMetadataRequest) {
  // given
  MetadataRequest request{{{"t1", "t2", "t3"}}, true};
  request.apiVersion() = 4;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === OFFSET COMMIT (8) =======================================================

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequest) {
  // given
  OffsetCommitRequest request{
      "group_id",
      1234,
      "member",
      2345,
      {{{"topic1", {{{0, 10, "m1"}, {2, 20, "m2"}}}}, {"topic2", {{{3, 30, "m3"}}}}}}};
  request.apiVersion() = 3;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === OFFSET FETCH (9) ========================================================

TEST_F(RequestDecoderTest, shouldParseOffsetFetchRequest) {
  // given
  OffsetFetchRequest request{"group_id", {{{"topic1", {{0, 1, 2}}}, {"topic2", {{3, 4}}}}}};

  request.apiVersion() = 3;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === API VERSIONS (18) =======================================================

TEST_F(RequestDecoderTest, shouldParseApiVersionsRequest) {
  // given
  ApiVersionsRequest request{};
  request.apiVersion() = 1;
  request.correlationId() = 42;
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
  RequestSerializer serializer;
  ApiVersionsRequest request{};
  request.apiVersion() = 1;
  request.correlationId() = 42;
  request.clientId() = "client-id";

  char data[1024];
  size_t written = serializer.encode(request, data);

  buffer_.add(data, written);

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
