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

// === PRODUCE (0) =============================================================

TEST_F(RequestDecoderTest, shouldParseProduceRequestV0toV2) {
  // given
  NULLABLE_ARRAY<FatProduceTopic> topics{
      {{"t1", {{{0, NULLABLE_BYTES(100)}, {1, NULLABLE_BYTES(200)}}}},
       {"t2", {{{0, NULLABLE_BYTES(300)}}}}}};
  FatProduceRequest request{10, 20, topics};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseProduceRequestV3) {
  // given
  NULLABLE_ARRAY<FatProduceTopic> topics{
      {{"t1", {{{0, NULLABLE_BYTES(100)}, {1, NULLABLE_BYTES(200)}}}},
       {"t2", {{{0, NULLABLE_BYTES(300)}}}}}};
  // transaction_id in V3
  FatProduceRequest request{"txid", 10, 20, topics};
  request.apiVersion() = 3;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === FETCH (1) ===============================================================

TEST_F(RequestDecoderTest, shouldParseFetchRequestV0toV2) {
  // given
  FetchRequest request{1,
                       1000,
                       10,
                       {{
                           {"topic1", {{{10, 20, 2000}}}},
                           {"topic1", {{{11, 21, 2001}, {12, 22, 2002}}}},
                           {"topic1", {{{13, 23, 2003}}}},
                       }}};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseFetchRequestV3) {
  // given
  FetchRequest request{1,
                       1000,
                       10,
                       20, // max_bytes in V3
                       {{
                           {"topic1", {{{10, 20, 2000}}}},
                           {"topic1", {{{11, 21, 2001}, {12, 22, 2002}}}},
                           {"topic1", {{{13, 23, 2003}}}},
                       }}};
  request.apiVersion() = 3;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseFetchRequestV4) {
  // given
  FetchRequest request{1,
                       1000,
                       10,
                       20,
                       2, // isolation level in V4
                       {{
                           {"topic1", {{{10, 20, 2000}}}},
                           {"topic1", {{{11, 21, 2001}, {12, 22, 2002}}}},
                           {"topic1", {{{13, 23, 2003}}}},
                       }}};
  request.apiVersion() = 4;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseFetchRequestV5) {
  // given
  FetchRequest request{1,
                       1000,
                       10,
                       20,
                       2,
                       {{
                           // log_start_offset_ in partition data in V5
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

TEST_F(RequestDecoderTest, shouldParseListOffsetsRequestV0) {
  // given
  ListOffsetsRequest request{10,
                             {{
                                 // partition contains max_num_offsets in v0 only
                                 {"topic1", {{{1, 1000, 10}, {2, 2000, 20}}}},
                                 {"topic2", {{{3, 3000, 30}}}},
                             }}};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseListOffsetsRequestV1) {
  // given
  ListOffsetsRequest request{10,
                             {{
                                 // max_num_offsets removed in v1
                                 {"topic1", {{{1, 1000}, {2, 2000}}}},
                                 {"topic2", {{{3, 3000}}}},
                             }}};
  request.apiVersion() = 1;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseListOffsetsRequestV2) {
  // given
  ListOffsetsRequest request{10,
                             2, // isolation level in v2
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

TEST_F(RequestDecoderTest, shouldParseMetadataRequestV0toV3) {
  // given
  MetadataRequest request{{{"t1", "t2", "t3"}}};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseMetadataRequestV4) {
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

// === LEADER-AND-ISR (4) ======================================================

TEST_F(RequestDecoderTest, shouldParseLeaderAndIsrRequestV0) {
  // given
  MetadataPartitionState ps1{"t1", 0, 1000, 1, 2000, {{0, 1}}, 3000, {{0, 1, 2, 3, 4}}};
  MetadataPartitionState ps2{"t2", 1, 4000, 2, 5000, {{6, 7}}, 6000, {{6, 7, 8, 9, 10}}};
  NULLABLE_ARRAY<MetadataPartitionState> partition_states{{ps1, ps2}};
  NULLABLE_ARRAY<LeaderAndIsrLiveLeader> live_leaders{
      {{1, "host1", 9092}, {2, "host2", 9093}, {3, "host3", 9094}}};
  LeaderAndIsrRequest request{20, 1000, partition_states, live_leaders};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === STOP REPLICA (5) ========================================================

TEST_F(RequestDecoderTest, shouldParseStopReplicaRequestV0) {
  // given
  NULLABLE_ARRAY<StopReplicaPartition> partitions{{{"t1", 0}, {"t2", 3}}};
  StopReplicaRequest request{10, 1000, true, partitions};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === UPDATE METADATA (6) =====================================================

TEST_F(RequestDecoderTest, shouldParseUpdateMetadataRequestV0) {
  // given
  MetadataPartitionState ps1{"t1", 0, 1000, 1, 2000, {{0, 1}}, 3000, {{0, 1, 2, 3, 4}}};
  MetadataPartitionState ps2{"t2", 1, 4000, 2, 5000, {{6, 7}}, 6000, {{6, 7, 8, 9, 10}}};
  NULLABLE_ARRAY<MetadataPartitionState> partition_states{{ps1, ps2}};
  NULLABLE_ARRAY<UpdateMetadataLiveBroker> live_brokers{
      {{1, "host1", 9092}, {2, "host2", 9093}, {3, "host3", 9094}}};
  UpdateMetadataRequest request{20, 1000, partition_states, live_brokers};
  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseUpdateMetadataRequestV1) {
  // given
  MetadataPartitionState ps1{"t1", 0, 1000, 1, 2000, {{0, 1}}, 3000, {{0, 1, 2, 3, 4}}};
  MetadataPartitionState ps2{"t2", 1, 4000, 2, 5000, {{6, 7}}, 6000, {{6, 7, 8, 9, 10}}};
  NULLABLE_ARRAY<MetadataPartitionState> partition_states{{ps1, ps2}};
  NULLABLE_ARRAY<UpdateMetadataLiveBroker> live_brokers{
      {{1, {{{9092, "h1", 0}, {9093, "h1", 1}}}}, // endpoints added in v1, host & port removed
       {2, {{{9092, "h2", 2}, {9093, "h2", 3}}}}}};
  UpdateMetadataRequest request{20, 1000, partition_states, live_brokers};
  request.apiVersion() = 1;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseUpdateMetadataRequestV2) {
  // given
  MetadataPartitionState ps1{"t1", 0, 1000, 1, 2000, {{0, 1}}, 3000, {{0, 1, 2, 3, 4}}};
  MetadataPartitionState ps2{"t2", 1, 4000, 2, 5000, {{6, 7}}, 6000, {{6, 7, 8, 9, 10}}};
  NULLABLE_ARRAY<MetadataPartitionState> partition_states{{ps1, ps2}};
  NULLABLE_ARRAY<UpdateMetadataLiveBroker> live_brokers{
      {{1, {{{9092, "h1", 0}, {9093, "h1", 1}}}, "rack1"}, // rack added in v2
       {2, {{{9092, "h2", 2}, {9093, "h2", 3}}}, absl::nullopt}}};
  UpdateMetadataRequest request{20, 1000, partition_states, live_brokers};
  request.apiVersion() = 2;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

TEST_F(RequestDecoderTest, shouldParseUpdateMetadataRequestV3) {
  // given
  MetadataPartitionState ps1{"t1", 0, 1000, 1, 2000, {{0, 1}}, 3000, {{0, 1, 2, 3, 4}}};
  MetadataPartitionState ps2{"t2", 1, 4000, 2, 5000, {{6, 7}}, 6000, {{6, 7, 8, 9, 10}}};
  NULLABLE_ARRAY<MetadataPartitionState> partition_states{{ps1, ps2}};
  NULLABLE_ARRAY<UpdateMetadataLiveBroker> live_brokers{
      {{1,
        {{{9092, "h1", "plain", 0}, {9093, "h1", "ssl", 1}}},
        "rack1"}, // listener_name added in v2
       {2, {{{9092, "h2", "name3", 2}, {9093, "h2", "name4", 3}}}, absl::nullopt}}};
  UpdateMetadataRequest request{20, 1000, partition_states, live_brokers};
  request.apiVersion() = 3;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === CONTROLLED SHUTDOWN (7) =================================================

TEST_F(RequestDecoderTest, shouldParseControlledShutdownRequestV1) {
  // given
  ControlledShutdownRequest request{1};
  request.apiVersion() = 1;
  request.correlationId() = 42;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

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

TEST_F(RequestDecoderTest, shouldParseOffsetCommitRequestV2toV3) {
  // given
  NULLABLE_ARRAY<OffsetCommitTopic> topics{
      {{"topic1", {{{0, 10, "m1"}, {2, 20, "m2"}}}}, {"topic2", {{{3, 30, "m3"}}}}}};
  OffsetCommitRequest request{"group_id", 1234, "member",
                              2345, // retention_time
                              topics};
  request.apiVersion() = 2;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === OFFSET FETCH (9) ========================================================

TEST_F(RequestDecoderTest, shouldParseOffsetFetchRequestV0toV3) {
  // given
  OffsetFetchRequest request{"group_id", {{{"topic1", {{0, 1, 2}}}, {"topic2", {{3, 4}}}}}};

  request.apiVersion() = 0;
  request.correlationId() = 10;
  request.clientId() = "client-id";

  // when
  auto received = serializeAndDeserialize(request);

  // then
  ASSERT_NE(received, nullptr);
  ASSERT_EQ(*received, request);
}

// === API VERSIONS (18) =======================================================

TEST_F(RequestDecoderTest, shouldParseApiVersionsRequestV0toV1) {
  // given
  ApiVersionsRequest request{};
  request.apiVersion() = 0;
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
  RequestEncoder serializer{buffer_};
  ApiVersionsRequest request{};
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
