#include "extensions/filters/network/kafka/mesh/upstream_kafka_client.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;
using testing::ReturnNull;
using testing::Throw;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MockKafkaProducerWrapper : public KafkaProducerWrapper {
public:
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (const std::string, int32_t, int, void*, size_t, const void*, size_t, int64_t,
               void*));
  MOCK_METHOD(int, poll, (int));
};

class MockLibRdKafkaUtils : public LibRdKafkaUtils {
public:
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfProperty,
              (RdKafka::Conf&, const std::string&, const std::string&, std::string&), (const));
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfDeliveryCallback,
              (RdKafka::Conf&, RdKafka::DeliveryReportCb*, std::string&), (const));
  MOCK_METHOD((std::unique_ptr<KafkaProducerWrapper>), createProducer,
              (RdKafka::Conf*, std::string& errstr), (const));
};

class MockProduceFinishCb : public ProduceFinishCb {
public:
  MOCK_METHOD(bool, accept, (const DeliveryMemento&));
};

class UpstreamKafkaClientTest : public testing::Test {
protected:
  Event::MockDispatcher dispatcher_;
  Thread::ThreadFactory& thread_factory_ = Thread::threadFactoryForTest();
  MockLibRdKafkaUtils kafka_utils_;
  RawKafkaProducerConfig config_ = {{"key1", "value1"}, {"key2", "value2"}};

  std::unique_ptr<MockKafkaProducerWrapper> producer_ptr =
      std::make_unique<MockKafkaProducerWrapper>();
  MockKafkaProducerWrapper& producer = *producer_ptr;

  std::shared_ptr<MockProduceFinishCb> origin_ = std::make_shared<MockProduceFinishCb>();

  // Helper method - allows creation of RichKafkaProducer without problems.
  void setupConstructorExpectations() {
    EXPECT_CALL(kafka_utils_, setConfProperty(_, "key1", "value1", _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));
    EXPECT_CALL(kafka_utils_, setConfProperty(_, "key2", "value2", _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));
    EXPECT_CALL(kafka_utils_, setConfDeliveryCallback(_, _, _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));

    EXPECT_CALL(producer, poll(_)).Times(AnyNumber());
    EXPECT_CALL(kafka_utils_, createProducer(_, _))
        .WillOnce(Return(testing::ByMove(std::move(producer_ptr))));
  }
};

TEST_F(UpstreamKafkaClientTest, ShouldConstructWithoutProblems) {
  // given
  setupConstructorExpectations();

  // when, then - producer got created without problems.
  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};
}

TEST_F(UpstreamKafkaClientTest, ShouldSendRecordsAndReceiveConfirmations) {
  // given
  setupConstructorExpectations();
  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};

  // when, then - should send request without problems.
  EXPECT_CALL(producer, produce("t1", 13, 0, _, _, _, _, _, _))
      .Times(3)
      .WillRepeatedly(Return(RdKafka::ERR_NO_ERROR));
  const std::vector<std::string> payloads = {"value1", "value2", "value3"};
  for (const auto& arg : payloads) {
    testee.send(origin_, "t1", 13, "KEY", arg);
  }
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), payloads.size());

  // when, then - should process confirmations.
  EXPECT_CALL(*origin_, accept(_)).Times(3).WillRepeatedly(Return(true));
  for (const auto& arg : payloads) {
    const DeliveryMemento memento = {arg.c_str(), RdKafka::ERR_NO_ERROR, 0};
    testee.processDelivery(memento);
  }
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), 0);
}

TEST_F(UpstreamKafkaClientTest, ShouldCheckCallbacksForDeliveries) {
  // given
  setupConstructorExpectations();
  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};

  // when, then - should send request without problems.
  EXPECT_CALL(producer, produce("t1", 13, 0, _, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(Return(RdKafka::ERR_NO_ERROR));
  const std::vector<std::string> payloads = {"value1", "value2"};
  auto origin1 = std::make_shared<MockProduceFinishCb>();
  auto origin2 = std::make_shared<MockProduceFinishCb>();
  testee.send(origin1, "t1", 13, "KEY", payloads[0]);
  testee.send(origin2, "t1", 13, "KEY", payloads[1]);
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), payloads.size());

  // when, then - should process confirmations (notice we pass second memento first).
  EXPECT_CALL(*origin1, accept(_)).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*origin2, accept(_)).WillOnce(Return(true));
  const DeliveryMemento memento1 = {payloads[1].c_str(), RdKafka::ERR_NO_ERROR, 0};
  testee.processDelivery(memento1);
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), 1);
  const DeliveryMemento memento2 = {payloads[0].c_str(), RdKafka::ERR_NO_ERROR, 0};
  testee.processDelivery(memento2);
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), 0);
}

TEST_F(UpstreamKafkaClientTest, ShouldHandleProduceFailures) {
  // given
  setupConstructorExpectations();
  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};

  // when, then - if there are problems while sending, notify the source immediately.
  EXPECT_CALL(producer, produce("t1", 42, 0, _, _, _, _, _, _))
      .WillOnce(Return(RdKafka::ERR_LEADER_NOT_AVAILABLE));
  EXPECT_CALL(*origin_, accept(_)).WillOnce(Return(true));
  testee.send(origin_, "t1", 42, "KEY", "VALUE");
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), 0);
}

// Impl note: this isn't pretty, but we need it.
class MockKafkaMessage : public RdKafka::Message {
public:
  MOCK_METHOD(std::string, errstr, (), (const));
  MOCK_METHOD(RdKafka::ErrorCode, err, (), (const));
  MOCK_METHOD(RdKafka::Topic*, topic, (), (const));
  MOCK_METHOD(std::string, topic_name, (), (const));
  MOCK_METHOD(int32_t, partition, (), (const));
  MOCK_METHOD(void*, payload, (), (const));
  MOCK_METHOD(size_t, len, (), (const));
  MOCK_METHOD(const std::string*, key, (), (const));
  MOCK_METHOD(const void*, key_pointer, (), (const));
  MOCK_METHOD(size_t, key_len, (), (const));
  MOCK_METHOD(int64_t, offset, (), (const));
  MOCK_METHOD(RdKafka::MessageTimestamp, timestamp, (), (const));
  MOCK_METHOD(void*, msg_opaque, (), (const));
  MOCK_METHOD(int64_t, latency, (), (const));
  MOCK_METHOD(struct rd_kafka_message_s*, c_ptr, ());
  MOCK_METHOD(RdKafka::Message::Status, status, (), (const));
  MOCK_METHOD(RdKafka::Headers*, headers, ());
  MOCK_METHOD(RdKafka::Headers*, headers, (RdKafka::ErrorCode*));
};

TEST_F(UpstreamKafkaClientTest, ShouldHandleKafkaCallback) {
  // given
  setupConstructorExpectations();
  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};
  testing::NiceMock<MockKafkaMessage> message;

  // when, then - notification is passed to dispatcher.
  EXPECT_CALL(dispatcher_, post(_));
  testee.dr_cb(message);
}

// This handles situations when users pass bad config to raw producer.
TEST_F(UpstreamKafkaClientTest, ShouldThrowIfSettingPropertiesFails) {
  // given
  EXPECT_CALL(kafka_utils_, setConfProperty(_, _, _, _))
      .WillOnce(Return(RdKafka::Conf::CONF_INVALID));

  // when, then - exception gets thrown during construction.
  EXPECT_THROW(RichKafkaProducer(dispatcher_, thread_factory_, config_, kafka_utils_),
               EnvoyException);
}

TEST_F(UpstreamKafkaClientTest, ShouldThrowIfSettingDeliveryCallbackFails) {
  // given
  EXPECT_CALL(kafka_utils_, setConfProperty(_, _, _, _))
      .WillRepeatedly(Return(RdKafka::Conf::CONF_OK));
  EXPECT_CALL(kafka_utils_, setConfDeliveryCallback(_, _, _))
      .WillOnce(Return(RdKafka::Conf::CONF_INVALID));

  // when, then - exception gets thrown during construction.
  EXPECT_THROW(RichKafkaProducer(dispatcher_, thread_factory_, config_, kafka_utils_),
               EnvoyException);
}

TEST_F(UpstreamKafkaClientTest, ShouldThrowIfRawProducerConstructionFails) {
  // given
  EXPECT_CALL(kafka_utils_, setConfProperty(_, _, _, _))
      .WillRepeatedly(Return(RdKafka::Conf::CONF_OK));
  EXPECT_CALL(kafka_utils_, setConfDeliveryCallback(_, _, _))
      .WillOnce(Return(RdKafka::Conf::CONF_OK));
  EXPECT_CALL(kafka_utils_, createProducer(_, _)).WillOnce(ReturnNull());

  // when, then - exception gets thrown during construction.
  EXPECT_THROW(RichKafkaProducer(dispatcher_, thread_factory_, config_, kafka_utils_),
               EnvoyException);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
