#include "extensions/filters/network/kafka/external/responses.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

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

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
