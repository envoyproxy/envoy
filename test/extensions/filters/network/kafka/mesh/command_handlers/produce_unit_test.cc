#include <set>

#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/external/responses.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

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
  MOCK_METHOD((std::vector<OutboundRecord>), extractRecords, (const std::vector<TopicProduceData>&),
              (const));
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
  const std::vector<OutboundRecord> records = {};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

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
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb"};
  const OutboundRecord r2 = {"t2", 42, "ccc", "ddd"};
  const std::vector<OutboundRecord> records = {r1, r2};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

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

// Typical flow without errors.
// The produce request has 2 records, both pointing to same partition.
// Given that usually we cannot make any guarantees on how Kafka producer is going to append the
// records (as it depends on configuration like max number of records in flight), the first record
// is going to be saved on a bigger offset.
TEST_F(ProduceUnitTest, ShouldMergeOutboundRecordResponses) {
  // given
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb"};
  const OutboundRecord r2 = {r1.topic_, r1.partition_, "ccc", "ddd"};
  const std::vector<OutboundRecord> records = {r1, r2};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

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

  // when, then - first record should be delivered.
  const DeliveryMemento dm1 = {r1.value_.data(), RdKafka::ERR_NO_ERROR, 4242};
  EXPECT_TRUE(testee->accept(dm1));
  EXPECT_FALSE(testee->finished());

  const DeliveryMemento dm2 = {r2.value_.data(), RdKafka::ERR_NO_ERROR, 1313};
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
  EXPECT_EQ(responses[0].partitions_.size(), 1);
  EXPECT_EQ(responses[0].partitions_[0].error_code_, RdKafka::ERR_NO_ERROR);
  EXPECT_EQ(responses[0].partitions_[0].base_offset_, 1313);
}

// Flow with errors.
// The produce request has 2 records, both pointing to same partition.
// The first record is going to fail.
// We are going to treat whole delivery as failure.
// Bear in mind second record could get accepted, this is a difference between normal client and
// proxy (this is going to be amended when we manage to send whole record batch).
TEST_F(ProduceUnitTest, ShouldHandleDeliveryErrors) {
  // given
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb"};
  const OutboundRecord r2 = {r1.topic_, r1.partition_, "ccc", "ddd"};
  const std::vector<OutboundRecord> records = {r1, r2};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

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
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb"};
  const std::vector<OutboundRecord> records = {r1};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

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

// Simple matcher that verifies that the input given is a collection containing correct number of
// unique (!) records for given topic-partition pairs.
MATCHER_P3(HasRecords, topic, partition, expected, "") {
  size_t expected_count = expected;
  std::set<absl::string_view> saved_key_pointers = {};
  std::set<absl::string_view> saved_value_pointers = {};
  size_t count = 0;

  for (const auto& record : arg) {
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

// Helper function to create a record batch that contains a single record with 5-byte key and 5-byte
// value.
Bytes makeGoodRecordBatch() {
  // Record batch bytes get ignored (apart from magic field), so we can put 0 there.
  Bytes result = Bytes(16 + 1 + 44);
  result[16] = 2; // Record batch magic value.
  Bytes real_data = {/* Length = 36 */ 72,
                     /* Attributes */ 0,
                     /* Timestamp delta */ 0,
                     /* Offset delta */ 0,
                     /* Key length = 5 */ 10,
                     107,
                     107,
                     107,
                     107,
                     107,
                     /* Value length = 5 */ 10,
                     118,
                     118,
                     118,
                     118,
                     118,
                     /* Headers count = 2 */ 4,
                     /* Header key length = 3 */ 6,
                     49,
                     49,
                     49,
                     /* Header value length = 5 */ 10,
                     97,
                     97,
                     97,
                     97,
                     97,
                     /* Header key length = 3 */ 6,
                     50,
                     50,
                     50,
                     /* Header value length = 5 */ 10,
                     98,
                     98,
                     98,
                     98,
                     98};
  result.insert(result.end(), real_data.begin(), real_data.end());
  return result;
}

TEST(RecordExtractorImpl, shouldProcessRecordBytes) {
  // given
  const RecordExtractorImpl testee;

  const PartitionProduceData t1_ppd1 = {0, makeGoodRecordBatch()};
  const PartitionProduceData t1_ppd2 = {1, makeGoodRecordBatch()};
  const PartitionProduceData t1_ppd3 = {2, makeGoodRecordBatch()};
  const TopicProduceData tpd1 = {"topic1", {t1_ppd1, t1_ppd2, t1_ppd3}};

  // Weird input from client, protocol allows sending null value as bytes array.
  const PartitionProduceData t2_ppd = {20, absl::nullopt};
  const TopicProduceData tpd2 = {"topic2", {t2_ppd}};

  const std::vector<TopicProduceData> input = {tpd1, tpd2};

  // when
  const auto result = testee.extractRecords(input);

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
  const PartitionProduceData ppd = {0, bytes};
  const TopicProduceData tpd = {"topic", {ppd}};
  return {tpd};
}

TEST(RecordExtractorImpl, shouldHandleInvalidRecordBytes) {
  const RecordExtractorImpl testee;
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(1)), EnvoyException,
                          "no common fields");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(2)), EnvoyException,
                          "magic byte is not present");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(3)), EnvoyException,
                          "unknown magic value");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(4)), EnvoyException,
                          "no attribute fields");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(5)), EnvoyException,
                          "no length");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(6)), EnvoyException,
                          "attributes not present");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(7)), EnvoyException,
                          "header count not present");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(8)), EnvoyException,
                          "invalid header count");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(9)), EnvoyException,
                          "data left after consuming record");
}

// Minor helper function.
absl::string_view bytesToStringView(const Bytes& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

TEST(RecordExtractorImpl, shouldExtractElementData) {
  {
    const Bytes noBytes = Bytes(0);
    auto arg = bytesToStringView(noBytes);
    EXPECT_THROW_WITH_REGEX(RecordExtractorImpl::extractElement(arg), EnvoyException,
                            "byte array length not present");
  }
  {
    const Bytes nullValueBytes = {0b00000001}; // Length = -1.
    auto arg = bytesToStringView(nullValueBytes);
    EXPECT_EQ(RecordExtractorImpl::extractElement(arg), absl::string_view());
  }
  {
    const Bytes negativeLengthBytes = {0b01111111}; // Length = -64.
    auto arg = bytesToStringView(negativeLengthBytes);
    EXPECT_THROW_WITH_REGEX(RecordExtractorImpl::extractElement(arg), EnvoyException,
                            "byte array length less than -1: -64");
  }
  {
    const Bytes bigLengthBytes = {0b01111110}; // Length = 63.
    auto arg = bytesToStringView(bigLengthBytes);
    EXPECT_THROW_WITH_REGEX(RecordExtractorImpl::extractElement(arg), EnvoyException,
                            "byte array length larger than data provided: 63 vs 0");
  }
  {
    // Length = 4, 7 bytes follow, 4 should be consumed, 13s should stay.
    const Bytes goodBytes = {0b00001000, 42, 42, 42, 42, 13, 13, 13};
    auto arg = bytesToStringView(goodBytes);
    EXPECT_EQ(RecordExtractorImpl::extractElement(arg),
              absl::string_view(reinterpret_cast<const char*>(goodBytes.data() + 1), 4));
    EXPECT_EQ(arg.data(), reinterpret_cast<const char*>(goodBytes.data() + 5));
    EXPECT_EQ(arg.size(), 3);
  }
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
