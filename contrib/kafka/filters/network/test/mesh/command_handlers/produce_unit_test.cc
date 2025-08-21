#include <set>

#include "test/test_common/utility.h"

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce.h"
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
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

class MockRecordExtractor : public RecordExtractor {
public:
  MOCK_METHOD((std::vector<OutboundRecord>), extractRecords, (const std::vector<TopicProduceData>&),
              (const));
};

class MockUpstreamKafkaFacade : public UpstreamKafkaFacade {
public:
  MOCK_METHOD(KafkaProducer&, getProducerForTopic, (const std::string&));
};

class MockKafkaProducer : public KafkaProducer {
public:
  MOCK_METHOD(void, send, (const ProduceFinishCbSharedPtr, const OutboundRecord&), ());
  MOCK_METHOD(void, markFinished, (), ());
};

class ProduceUnitTest : public testing::Test {
protected:
  MockAbstractRequestListener filter_;
  MockUpstreamKafkaFacade upstream_kafka_facade_;
  MockRecordExtractor extractor_;
};

// This is very odd corner case, that should never happen
// (as ProduceRequests with no topics/records make no sense).
TEST_F(ProduceUnitTest, ShouldHandleProduceRequestWithNoRecords) {
  // given
  const std::vector<OutboundRecord> records = {};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  ProduceRequestHolder testee = {filter_, upstream_kafka_facade_, extractor_, message};

  // when, then - invoking should immediately notify the filter.
  EXPECT_CALL(filter_, onRequestReadyForAnswer());
  testee.startProcessing();

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
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb", {}};
  const OutboundRecord r2 = {"t2", 42, "ccc", "ddd", {}};
  const std::vector<OutboundRecord> records = {r1, r2};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, upstream_kafka_facade_, extractor_, message);

  // when, then - invoking should use producers to send records.
  MockKafkaProducer producer1;
  EXPECT_CALL(producer1, send(_, _));
  MockKafkaProducer producer2;
  EXPECT_CALL(producer2, send(_, _));
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r1.topic_))
      .WillOnce(ReturnRef(producer1));
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r2.topic_))
      .WillOnce(ReturnRef(producer2));
  testee->startProcessing();

  // when, then - request is not yet finished (2 records' delivery to be confirmed).
  EXPECT_FALSE(testee->finished());

  // when, then - first record should be delivered.
  const DeliveryMemento dm1 = {r1.value_.data(), 0, 123};
  EXPECT_TRUE(testee->accept(dm1));
  EXPECT_FALSE(testee->finished());

  const DeliveryMemento dm2 = {r2.value_.data(), 0, 234};
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
  EXPECT_EQ(responses[0].partition_responses_[0].error_code_, dm1.error_code_);
  EXPECT_EQ(responses[0].partition_responses_[0].base_offset_, dm1.offset_);
  EXPECT_EQ(responses[1].partition_responses_[0].error_code_, dm2.error_code_);
  EXPECT_EQ(responses[1].partition_responses_[0].base_offset_, dm2.offset_);
}

// Typical flow without errors.
// The produce request has 2 records, both pointing to same partition.
// Given that usually we cannot make any guarantees on how Kafka producer is going to append the
// records (as it depends on configuration like max number of records in flight), the first record
// is going to be saved on a bigger offset.
TEST_F(ProduceUnitTest, ShouldMergeOutboundRecordResponses) {
  // given
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb", {}};
  const OutboundRecord r2 = {r1.topic_, r1.partition_, "ccc", "ddd", {}};
  const std::vector<OutboundRecord> records = {r1, r2};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, upstream_kafka_facade_, extractor_, message);

  // when, then - invoking should use producers to send records.
  MockKafkaProducer producer;
  EXPECT_CALL(producer, send(_, _)).Times(2);
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r1.topic_))
      .WillRepeatedly(ReturnRef(producer));
  testee->startProcessing();

  // when, then - request is not yet finished (2 records' delivery to be confirmed).
  EXPECT_FALSE(testee->finished());

  // when, then - first record should be delivered.
  const DeliveryMemento dm1 = {r1.value_.data(), 0, 4242};
  EXPECT_TRUE(testee->accept(dm1));
  EXPECT_FALSE(testee->finished());

  const DeliveryMemento dm2 = {r2.value_.data(), 0, 1313};
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
  EXPECT_EQ(responses[0].partition_responses_.size(), 1);
  EXPECT_EQ(responses[0].partition_responses_[0].error_code_, 0);
  EXPECT_EQ(responses[0].partition_responses_[0].base_offset_, 1313);
}

// Flow with errors.
// The produce request has 2 records, both pointing to same partition.
// The first record is going to fail.
// We are going to treat whole delivery as failure.
// Bear in mind second record could get accepted, this is a difference between normal client and
// proxy (this is going to be amended when we manage to send whole record batch).
TEST_F(ProduceUnitTest, ShouldHandleDeliveryErrors) {
  // given
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb", {}};
  const OutboundRecord r2 = {r1.topic_, r1.partition_, "ccc", "ddd", {}};
  const std::vector<OutboundRecord> records = {r1, r2};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, upstream_kafka_facade_, extractor_, message);

  // when, then - invoking should use producers to send records.
  MockKafkaProducer producer;
  EXPECT_CALL(producer, send(_, _)).Times(2);
  EXPECT_CALL(upstream_kafka_facade_, getProducerForTopic(r1.topic_))
      .WillRepeatedly(ReturnRef(producer));
  testee->startProcessing();

  // when, then - request is not yet finished (2 records' delivery to be confirmed).
  EXPECT_FALSE(testee->finished());

  // when, then - first record fails.
  const DeliveryMemento dm1 = {r1.value_.data(), 42, 0};
  EXPECT_TRUE(testee->accept(dm1));
  EXPECT_FALSE(testee->finished());

  // when, then - second record succeeds (we are going to ignore the result).
  const DeliveryMemento dm2 = {r2.value_.data(), 0, 234};
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
  EXPECT_EQ(responses[0].partition_responses_[0].error_code_, dm1.error_code_);
}

// As with current version of Kafka library we have no capability of linking producer's notification
// to sent record (other than data address), the owner of this request is going to do a check across
// all owned requests. What means sometimes we might get asked to accept a memento of record that
// did not originate in this request, so it should be ignored.
TEST_F(ProduceUnitTest, ShouldIgnoreMementoFromAnotherRequest) {
  // given
  const OutboundRecord r1 = {"t1", 13, "aaa", "bbb", {}};
  const std::vector<OutboundRecord> records = {r1};
  EXPECT_CALL(extractor_, extractRecords(_)).WillOnce(Return(records));

  const RequestHeader header = {0, 0, 0, absl::nullopt};
  const ProduceRequest data = {0, 0, {}};
  const auto message = std::make_shared<Request<ProduceRequest>>(header, data);
  std::shared_ptr<ProduceRequestHolder> testee =
      std::make_shared<ProduceRequestHolder>(filter_, upstream_kafka_facade_, extractor_, message);

  // when, then - this record will not match anything.
  const DeliveryMemento dm = {nullptr, 0, 42};
  EXPECT_FALSE(testee->accept(dm));
  EXPECT_FALSE(testee->finished());
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
