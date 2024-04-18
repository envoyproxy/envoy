#include "test/mocks/event/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/blocking_counter.h"
#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client_impl.h"
#include "contrib/kafka/filters/network/test/mesh/kafka_mocks.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNull;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MockProduceFinishCb : public ProduceFinishCb {
public:
  MOCK_METHOD(bool, accept, (const DeliveryMemento&));
};

class UpstreamKafkaClientTest : public testing::Test {
protected:
  Event::MockDispatcher dispatcher_;
  Thread::ThreadFactory& thread_factory_ = Thread::threadFactoryForTest();
  NiceMock<MockLibRdKafkaUtils> kafka_utils_{};
  RawKafkaConfig config_ = {{"key1", "value1"}, {"key2", "value2"}};

  std::unique_ptr<MockKafkaProducer> producer_ptr_ = std::make_unique<MockKafkaProducer>();
  MockKafkaProducer& producer_ = *producer_ptr_;

  std::shared_ptr<MockProduceFinishCb> origin_ = std::make_shared<MockProduceFinishCb>();

  // Helper method - allows creation of RichKafkaProducer without problems.
  void setupConstructorExpectations() {
    EXPECT_CALL(kafka_utils_, setConfProperty(_, "key1", "value1", _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));
    EXPECT_CALL(kafka_utils_, setConfProperty(_, "key2", "value2", _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));
    EXPECT_CALL(kafka_utils_, setConfDeliveryCallback(_, _, _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));

    EXPECT_CALL(producer_, poll(_)).Times(AnyNumber());
    EXPECT_CALL(kafka_utils_, createProducer(_, _))
        .WillOnce(Return(testing::ByMove(std::move(producer_ptr_))));

    EXPECT_CALL(kafka_utils_, deleteHeaders(_)).Times(0);
  }
};

OutboundRecord makeRecord(const std::string& payload) { return {"topic", 13, payload, "key", {}}; }

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
  EXPECT_CALL(producer_, produce("topic", 13, _, _, _, _, _, _, _, _))
      .Times(3)
      .WillRepeatedly(Return(RdKafka::ERR_NO_ERROR));
  const std::vector<std::string> payloads = {"value1", "value2", "value3"};
  for (const auto& arg : payloads) {
    testee.send(origin_, makeRecord(arg));
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
  EXPECT_CALL(producer_, produce("topic", 13, _, _, _, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(Return(RdKafka::ERR_NO_ERROR));
  const std::vector<std::string> payloads = {"value1", "value2"};
  auto origin1 = std::make_shared<MockProduceFinishCb>();
  auto origin2 = std::make_shared<MockProduceFinishCb>();
  testee.send(origin1, makeRecord(payloads[0]));
  testee.send(origin2, makeRecord(payloads[1]));
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
  EXPECT_CALL(producer_, produce("topic", 13, _, _, _, _, _, _, _, _))
      .WillOnce(Return(RdKafka::ERR_LEADER_NOT_AVAILABLE));
  EXPECT_CALL(kafka_utils_, deleteHeaders(_));
  EXPECT_CALL(*origin_, accept(_)).WillOnce(Return(true));
  testee.send(origin_, makeRecord("value"));
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), 0);
}

TEST_F(UpstreamKafkaClientTest, ShouldHandleKafkaCallback) {
  // given
  setupConstructorExpectations();
  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};
  NiceMock<MockKafkaMessage> message;

  // when, then - notification is passed to dispatcher.
  EXPECT_CALL(dispatcher_, post(_));
  testee.dr_cb(message);
}

TEST_F(UpstreamKafkaClientTest, ShouldHandleHeaderConversionFailures) {
  // given
  setupConstructorExpectations();
  EXPECT_CALL(kafka_utils_, convertHeaders(_)).WillOnce(Return(nullptr));

  RichKafkaProducer testee = {dispatcher_, thread_factory_, config_, kafka_utils_};

  // when, then - producer was not interacted with, response was sent immediately.
  EXPECT_CALL(producer_, produce(_, _, _, _, _, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*origin_, accept(_)).WillOnce(Return(true));
  testee.send(origin_, makeRecord("value"));
  EXPECT_EQ(testee.getUnfinishedRequestsForTest().size(), 0);
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

// Rich producer's constructor starts a monitoring thread.
// We are going to wait for at least one invocation of producer 'poll', so we are confident that it
// does monitoring. Then we are going to destroy the testee, and expect the thread to finish.
TEST_F(UpstreamKafkaClientTest, ShouldPollProducerForEventsUntilShutdown) {
  // given
  setupConstructorExpectations();

  absl::BlockingCounter counter{1};
  EXPECT_CALL(producer_, poll(_)).Times(AtLeast(1)).WillOnce([&counter]() {
    counter.DecrementCount();
    return 0;
  });

  // when
  {
    std::unique_ptr<RichKafkaProducer> testee =
        std::make_unique<RichKafkaProducer>(dispatcher_, thread_factory_, config_, kafka_utils_);
    counter.Wait();
  }

  // then - the above block actually finished, what means that the monitoring thread interacted with
  // underlying Kafka producer.
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
