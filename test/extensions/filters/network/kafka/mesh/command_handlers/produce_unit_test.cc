#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/external/responses.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <set>

#include "test/test_common/utility.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::Not;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockAbstractRequestListener : public AbstractRequestListener {
public:
  MOCK_METHOD(void, onRequest, (InFlightRequestSharedPtr));
  MOCK_METHOD(void, onRequestReadyForAnswer, ());
};

class MockRecordExtractor : public RecordExtractor {
public:
  MOCK_METHOD((std::vector<RecordFootmark>), computeFootmarks,
              (const std::vector<TopicProduceData>&), (const));
};

class MockUpstreamKafkaFacade : public UpstreamKafkaFacade {
public:
  MOCK_METHOD(RecordSink&, getProducerForTopic, (const std::string&));
};

class MockRecordSink : public RecordSink {
public:
  MOCK_METHOD(void, send,
              (const ProduceFinishCbSharedPtr, const std::string&, const int32_t,
               const absl::string_view, const absl::string_view));
};

class ProduceUnitTest : public testing::Test {
protected:
  MockAbstractRequestListener filter_;
  MockRecordExtractor extractor_;
  MockUpstreamKafkaFacade upstream_kafka_facade_;
};

// This is very odd corner case, that should never happen
// (as ProduceRequests with no topics/records make no sense).
TEST_F(ProduceUnitTest, ShouldHandleProduceRequestWithNoRecords) {
  // given
  MockRecordExtractor extractor;
  const std::vector<RecordFootmark> records = {};
  EXPECT_CALL(extractor_, computeFootmarks(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  ProduceRequestHolder testee = {filter_, extractor_, message};

  // when, then - invoking should immediately notify the filter.
  EXPECT_CALL(filter_, onRequestReadyForAnswer());
  testee.invoke(upstream_kafka_facade_);

  // when, then - request is finished because there was nothing to do.
  const bool finished = testee.finished();
  EXPECT_TRUE(finished);

  // when, then - answer is empty.
  const auto answer = testee.computeAnswer();
  EXPECT_EQ(answer->metadata_.api_key_, header.api_key_);
  EXPECT_EQ(answer->metadata_.correlation_id_, header.correlation_id_);
}

// Typical flow without errors.
// The produce request has 2 records, that should be mapped to 2 different Kafka installations.
// The response should contain the values returned by Kafka broker.
TEST_F(ProduceUnitTest, ShouldSendRecordsInNormalFlow) {
  // given
  const RecordFootmark r1 = {"t1", 13, "aaa", "bbb"};
  const RecordFootmark r2 = {"t2", 42, "ccc", "ddd"};
  const std::vector<RecordFootmark> records = {r1, r2};
  EXPECT_CALL(extractor_, computeFootmarks(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, extractor_, message);

  // when, then - invoking should use producers to send records.
  MockRecordSink record_sink1;
  EXPECT_CALL(record_sink1, send(_, r1.topic_, r1.partition_, _, _));
  MockRecordSink record_sink2;
  EXPECT_CALL(record_sink2, send(_, r2.topic_, r2.partition_, _, _));
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r1.topic_))
      .WillOnce(ReturnRef(record_sink1));
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r2.topic_))
      .WillOnce(ReturnRef(record_sink2));
  testee->invoke(upstream_kafka_facade_);

  // when, then - request is not yet finished (2 records' delivery to be confirmed).
  EXPECT_FALSE(testee->finished());

  // when, then - first record should be delivered.
  const DeliveryMemento dm1 = {r1.value_.data(), RdKafka::ERR_NO_ERROR, 123};
  EXPECT_TRUE(testee->accept(dm1));
  EXPECT_FALSE(testee->finished());

  const DeliveryMemento dm2 = {r2.value_.data(), RdKafka::ERR_NO_ERROR, 234};
  // After all the deliveries have been confirmed, the filter is getting notified.
  EXPECT_CALL(filter_, onRequestReadyForAnswer());
  EXPECT_TRUE(testee->accept(dm2));
  EXPECT_TRUE(testee->finished());

  // when, then - answer gets computed and contains results.
  const auto answer = testee->computeAnswer();
  EXPECT_EQ(answer->metadata_.api_key_, header.api_key_);
  EXPECT_EQ(answer->metadata_.correlation_id_, header.correlation_id_);

  const auto response = std::dynamic_pointer_cast<Response<ProduceResponse>>(answer);
  ASSERT_TRUE(response);
  const std::vector<TopicProduceResponse> responses = response->data_.responses_;
  EXPECT_EQ(responses.size(), 2);
  EXPECT_EQ(responses[0].partitions_[0].error_code_, dm1.error_code_);
  EXPECT_EQ(responses[0].partitions_[0].base_offset_, dm1.offset_);
  EXPECT_EQ(responses[1].partitions_[0].error_code_, dm2.error_code_);
  EXPECT_EQ(responses[1].partitions_[0].base_offset_, dm2.offset_);
}

// Flow with errors.
// The produce request has 2 records, both pointing to same partition.
// The first record is going to fail.
// We are going to treat whole delivery as failure.
// Bear in mind second record could get accepted, this is a difference between normal client and
// proxy (this is going to be amended when we manage to send whole record batch).
TEST_F(ProduceUnitTest, ShouldHandleDeliveryErrors) {
  // given
  const RecordFootmark r1 = {"t1", 13, "aaa", "bbb"};
  const RecordFootmark r2 = {r1.topic_, r1.partition_, "ccc", "ddd"};
  const std::vector<RecordFootmark> records = {r1, r2};
  EXPECT_CALL(extractor_, computeFootmarks(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, extractor_, message);

  // when, then - invoking should use producers to send records.
  MockRecordSink record_sink;
  EXPECT_CALL(record_sink, send(_, r1.topic_, r1.partition_, _, _)).Times(2);
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r1.topic_))
      .WillRepeatedly(ReturnRef(record_sink));
  testee->invoke(upstream_kafka_facade_);

  // when, then - request is not yet finished (2 records' delivery to be confirmed).
  EXPECT_FALSE(testee->finished());

  // when, then - first record fails.
  const DeliveryMemento dm1 = {r1.value_.data(), RdKafka::ERR_LEADER_NOT_AVAILABLE, 0};
  EXPECT_TRUE(testee->accept(dm1));
  EXPECT_FALSE(testee->finished());

  // when, then - second record succeeds (we are going to ignore the result).
  const DeliveryMemento dm2 = {r2.value_.data(), RdKafka::ERR_NO_ERROR, 234};
  // After all the deliveries have been confirmed, the filter is getting notified.
  EXPECT_CALL(filter_, onRequestReadyForAnswer());
  EXPECT_TRUE(testee->accept(dm2));
  EXPECT_TRUE(testee->finished());

  // when, then - answer gets computed and contains results.
  const auto answer = testee->computeAnswer();
  EXPECT_EQ(answer->metadata_.api_key_, header.api_key_);
  EXPECT_EQ(answer->metadata_.correlation_id_, header.correlation_id_);

  const auto response = std::dynamic_pointer_cast<Response<ProduceResponse>>(answer);
  ASSERT_TRUE(response);
  const std::vector<TopicProduceResponse> responses = response->data_.responses_;
  EXPECT_EQ(responses.size(), 1);
  EXPECT_EQ(responses[0].partitions_[0].error_code_, dm1.error_code_);
}

// As with current version of librdkafka we have no capability of linking producer's notification to
// sent record (other than data address), the owner of this request is going to do a check across
// all owned requests. What means sometimes we might get asked to accept a memento of record that
// did not originate in this request, so it should be ignored.
TEST_F(ProduceUnitTest, ShouldIgnoreMementoFromAnotherRequest) {
  // given
  const RecordFootmark r1 = {"t1", 13, "aaa", "bbb"};
  const std::vector<RecordFootmark> records = {r1};
  EXPECT_CALL(extractor_, computeFootmarks(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, extractor_, message);

  // when, then - this record will not match anything.
  const DeliveryMemento dm = {nullptr, RdKafka::ERR_NO_ERROR, 42};
  EXPECT_FALSE(testee->accept(dm));
  EXPECT_FALSE(testee->finished());
}

// Tests for record extractor.

MATCHER_P3(HasRecords, topic, partition, expected, "") {
  size_t expected_count = expected;
  std::set<absl::string_view> saved_key_pointers = {};
  std::set<absl::string_view> saved_value_pointers = {};
  size_t count = 0;

  for (const auto record : arg) {
    if (record.topic_ == topic && record.partition_ == partition) {
      saved_key_pointers.insert(record.key_);
      saved_value_pointers.insert(record.value_);
      ++count;
    }
  }

  if (expected_count != count) {
    return false;
  }
  if (expected_count != saved_key_pointers.size()) {
    return false;
  }
  return saved_key_pointers.size() == saved_value_pointers.size();
}

Bytes makeGoodRecordBatch() {
  // Record batch bytes get ignored (apart from magic field), so we can put 0 there.
  Bytes result = Bytes(16 + 1 + 44);
  result[16] = 2; // Record batch magic value.
  // 61
  Bytes real_data = {
    /* len = 36 as varint32 */ 72,
    /* attr */ 0,
    /* tsdelta */ 0,
    /* offsetdelta */ 0,
    /* key_len = 5 as varint32 */ 10, 107, 107, 107, 107, 107,
    /* value len = 5 as varint32 */ 10, 118, 118, 118, 118, 118,
    /* headers_count = 2 as varint32 */ 4,
    /* header_k len = 3 as varint32 */ 6, 49, 49, 49,
    /* header_v len = 5 as varint32 */ 10, 97, 97, 97, 97, 97,
    /* header_k len = 3 as varint32 */ 6, 50, 50, 50,
    /* header_v len = 5 as varint32 */ 10, 98, 98, 98, 98, 98
  };
  result.insert(result.end(), real_data.begin(), real_data.end());
  return result;
}

TEST(RecordExtractor, shouldProcessRecordData) {
  // given
  const RecordExtractorImpl testee;

  const PartitionProduceData t1_ppd1 = { 0, makeGoodRecordBatch() };
  const PartitionProduceData t1_ppd2 = { 1, makeGoodRecordBatch() };
  const PartitionProduceData t1_ppd3 = { 2, makeGoodRecordBatch() };
  const TopicProduceData tpd1 = { "topic1", { t1_ppd1, t1_ppd2, t1_ppd3 } };

  // Weird input from client, protocol allows sending null value as bytes array.
  const PartitionProduceData t2_ppd = { 20, absl::nullopt };
  const TopicProduceData tpd2 = { "topic2", { t2_ppd } };

  const std::vector<TopicProduceData> input = { tpd1, tpd2 };

  // when
  const auto result = testee.computeFootmarks(input);

  // then
  EXPECT_THAT(result, HasRecords("topic1", 0, 1));
  EXPECT_THAT(result, HasRecords("topic1", 1, 1));
  EXPECT_THAT(result, HasRecords("topic1", 2, 1));
  EXPECT_THAT(result, HasRecords("topic2", 20, 0));
}

/**
 * Helper function to make record batch (batch contains 1+ records).
 * We use 'stage' parameter to make it a single function with various failure modes.
 */
const std::vector<TopicProduceData> makeTopicProduceData(const unsigned int stage) {
  Bytes bytes = makeGoodRecordBatch();
  if (1 == stage) {
    // No common fields before magic.
    bytes.erase(bytes.begin(), bytes.end());
  }
  if (2 == stage) {
    // No magic.
    bytes.erase(bytes.begin() + 16, bytes.end());
  }
  if (3 == stage) {
    // Bad magic.
    bytes[16] = 42;
  }
  if (4 == stage) {
    // No common fields after magic.
    bytes.erase(bytes.begin() + 17, bytes.end());
  }
  if (5 == stage) {
    // No record length after common fields.
    bytes[61] = 128;
    bytes.erase(bytes.begin() + 62, bytes.end());
  }
  if (6 == stage) {
    // Attributes fields missing.
    bytes.erase(bytes.begin() + 62, bytes.end());
  }
  if (7 == stage) {
    bytes[77] = 128;
    bytes.erase(bytes.begin() + 77, bytes.end());
  }
  if (8 == stage) {
    // Negative variable length integer.
    bytes[77] = 17;
  }
  if (9 == stage) {
    // Last header value is going to be shorter, so there will be one unconsumed byte.
    bytes[92] = 8;
  }
  if (10 == stage) {
    bytes[92] = 128;
    bytes.erase(bytes.begin() + 92, bytes.end());
  }
  if (11 == stage) {
    // Data length is said to be 16, while there will be only 5 bytes in last header.
    bytes[92] = 32;
  }
  if (12 == stage) {
    // Negative variable length integer.
    bytes[92] = 17;
  }
  const PartitionProduceData ppd = { 0, bytes };
  const TopicProduceData tpd = { "topic", { ppd }};
  return { tpd };
}

TEST(RecordExtractor, shouldHandleFailureScenarios) {
  const RecordExtractorImpl testee;
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(1)), EnvoyException, "no common fields");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(2)), EnvoyException, "magic byte is not present");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(3)), EnvoyException, "unknown magic value");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(4)), EnvoyException, "no attribute fields");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(5)), EnvoyException, "no length");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(6)), EnvoyException, "attributes not present");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(7)), EnvoyException, "header count not present");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(8)), EnvoyException, "invalid header count");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(9)), EnvoyException, "data left after consuming record");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(10)), EnvoyException, "byte array length not present");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(11)), EnvoyException, "byte array length larger than data provided");
  EXPECT_THROW_WITH_REGEX(testee.computeFootmarks(makeTopicProduceData(12)), EnvoyException, "byte array length less than -1");
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
