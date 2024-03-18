#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/thread/thread.h"

#include "test/mocks/thread/mocks.h"

#include "contrib/kafka/filters/network/source/mesh/shared_consumer_manager_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

using testing::_;
using testing::NiceMock;
using testing::Return;

class MockUpstreamKafkaConfiguration : public UpstreamKafkaConfiguration {
public:
  MOCK_METHOD(absl::optional<ClusterConfig>, computeClusterConfigForTopic, (const std::string&),
              (const));
  MOCK_METHOD((std::pair<std::string, int32_t>), getAdvertisedAddress, (), (const));
};

class MockKafkaConsumerFactory : public KafkaConsumerFactory {
public:
  MOCK_METHOD(KafkaConsumerPtr, createConsumer,
              (InboundRecordProcessor&, Thread::ThreadFactory&, const std::string&, const int32_t,
               const RawKafkaConfig&),
              (const));
};

class SharedConsumerManagerTest : public testing::Test {
protected:
  Thread::MockThreadFactory thread_factory_;
  MockUpstreamKafkaConfiguration configuration_;
  MockKafkaConsumerFactory consumer_factory_;

  std::unique_ptr<SharedConsumerManagerImpl> makeTestee() {
    return std::make_unique<SharedConsumerManagerImpl>(configuration_, thread_factory_,
                                                       consumer_factory_);
  }
};

TEST_F(SharedConsumerManagerTest, ShouldRegisterTopicOnlyOnce) {
  // given
  const std::string topic1 = "topic1";
  const std::string topic2 = "topic2";

  const ClusterConfig cluster_config = {"cluster", 1, {}, {}};
  EXPECT_CALL(configuration_, computeClusterConfigForTopic(topic1))
      .WillOnce(Return(cluster_config));
  EXPECT_CALL(configuration_, computeClusterConfigForTopic(topic2))
      .WillOnce(Return(cluster_config));

  EXPECT_CALL(consumer_factory_, createConsumer(_, _, _, _, _)).Times(2).WillRepeatedly([]() {
    return nullptr;
  });

  auto testee = makeTestee();

  // when
  for (int i = 0; i < 3; ++i) {
    testee->registerConsumerIfAbsent(topic1);
  }
  for (int i = 0; i < 3; ++i) {
    testee->registerConsumerIfAbsent(topic2);
  }

  // then
  ASSERT_EQ(testee->getConsumerCountForTest(), 2);
}

TEST_F(SharedConsumerManagerTest, ShouldHandleMissingConfig) {
  // given
  const std::string topic = "topic";

  EXPECT_CALL(configuration_, computeClusterConfigForTopic(topic)).WillOnce(Return(absl::nullopt));

  EXPECT_CALL(consumer_factory_, createConsumer(_, _, _, _, _)).Times(0);

  auto testee = makeTestee();

  // when, then - construction throws and nothing gets registered.
  EXPECT_THROW(testee->registerConsumerIfAbsent(topic), EnvoyException);
  ASSERT_EQ(testee->getConsumerCountForTest(), 0);
}

class MockRecordCb : public RecordCb {
public:
  MOCK_METHOD(CallbackReply, receive, (InboundRecordSharedPtr), ());
  MOCK_METHOD(TopicToPartitionsMap, interest, (), (const));
  MOCK_METHOD(std::string, toString, (), (const));
};

TEST_F(SharedConsumerManagerTest, ShouldProcessCallback) {
  // given
  const std::string topic1 = "topic1";
  const std::string topic2 = "topic2";

  const ClusterConfig cluster_config = {"cluster", 1, {}, {}};
  EXPECT_CALL(configuration_, computeClusterConfigForTopic(topic1))
      .WillOnce(Return(cluster_config));
  EXPECT_CALL(configuration_, computeClusterConfigForTopic(topic2))
      .WillOnce(Return(cluster_config));
  EXPECT_CALL(consumer_factory_, createConsumer(_, _, _, _, _)).Times(2);

  auto testee = makeTestee();

  const auto cb = std::make_shared<NiceMock<MockRecordCb>>();
  const TopicToPartitionsMap tp = {{topic1, {0, 1}}, {topic2, {0, 1, 2, 3, 4}}};
  EXPECT_CALL(*cb, interest).WillRepeatedly(Return(tp));

  // when
  testee->processCallback(cb);

  // then
  ASSERT_EQ(testee->getConsumerCountForTest(), 2);
}

// RecordDistributorTest

class RecordDistributorTest : public testing::Test {
protected:
  RecordDistributorPtr makeTestee() { return std::make_unique<RecordDistributor>(); }

  RecordDistributorPtr makeTestee(const PartitionMap<RecordCbSharedPtr>& callbacks) {
    return std::make_unique<RecordDistributor>(callbacks, PartitionMap<InboundRecordSharedPtr>());
  }

  RecordDistributorPtr makeTestee(const PartitionMap<InboundRecordSharedPtr>& records) {
    return std::make_unique<RecordDistributor>(PartitionMap<RecordCbSharedPtr>(), records);
  }

  std::shared_ptr<MockRecordCb> makeCb() {
    auto result = std::make_shared<NiceMock<MockRecordCb>>();
    ON_CALL(*result, receive(_)).WillByDefault(Return(CallbackReply::Rejected));
    return result;
  }

  InboundRecordSharedPtr makeRecord(const std::string& topic, const int32_t partition) {
    return std::make_shared<InboundRecord>(topic, partition, 0, absl::nullopt, absl::nullopt);
  }
};

TEST_F(RecordDistributorTest, ShouldNotWaitUntilInterestIfThereIsAny) {
  // given
  const std::string topic = "aaa";
  PartitionMap<RecordCbSharedPtr> callbacks;
  callbacks[{topic, 0}] = {makeCb()};
  callbacks[{"bbb", 0}] = {makeCb()};
  const auto testee = makeTestee(callbacks);

  // when
  const auto result = testee->waitUntilInterest(topic, 500);

  // then
  ASSERT_TRUE(result);
}

TEST_F(RecordDistributorTest, ShouldWaitUntilInterestIfThereIsNone) {
  // given
  const auto timeout_ms = 500;
  PartitionMap<RecordCbSharedPtr> callbacks;
  callbacks[{"bbb", 0}] = {makeCb()};
  const auto testee = makeTestee(callbacks);

  // when
  namespace ch = std::chrono;
  const auto start = ch::high_resolution_clock::now();
  const auto result = testee->waitUntilInterest("aaa", timeout_ms);
  const auto stop = ch::high_resolution_clock::now();

  // then
  ASSERT_FALSE(result);
  const auto duration_ms = (ch::duration_cast<ch::milliseconds>(stop - start)).count();
  ASSERT_GT(duration_ms, timeout_ms * 0.8);
}

TEST_F(RecordDistributorTest, ShouldStoreRecords) {
  // given
  const auto testee = makeTestee();

  // when
  for (int i = 0; i < 10; ++i) {
    const auto record = makeRecord("topic", 13);
    testee->receive(record);
  }

  // then
  ASSERT_EQ(testee->getRecordCountForTest("topic", 13), 10);
}

TEST_F(RecordDistributorTest, ShouldPassRecordToCallbacksAndRemoveIfFinished) {
  // given
  PartitionMap<RecordCbSharedPtr> callbacks;
  auto callback = makeCb();
  EXPECT_CALL(*callback, receive(_)).WillOnce(Return(CallbackReply::AcceptedAndFinished));
  callbacks[{"topic", 0}] = {callback};
  callbacks[{"topic", 1}] = {makeCb(), callback};
  callbacks[{"topic", 2}] = {callback, makeCb()};
  callbacks[{"topic", 3}] = {makeCb(), callback, makeCb()};
  const auto testee = makeTestee(callbacks);
  const auto record = makeRecord("topic", 2);

  // when
  testee->receive(record);

  // then
  // Record was not stored.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 2), -1);
  // Our callback got removed, others stay.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 2), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 3), 2);
}

// Compared to previous test - callback does not get removed here.
TEST_F(RecordDistributorTest, ShouldPassRecordToCallbacksAndKeepThem) {
  // given
  PartitionMap<RecordCbSharedPtr> callbacks;
  auto callback = makeCb();
  EXPECT_CALL(*callback, receive(_)).WillOnce(Return(CallbackReply::AcceptedAndWantMore));
  callbacks[{"topic", 0}] = {callback};
  callbacks[{"topic", 1}] = {makeCb(), callback};
  callbacks[{"topic", 2}] = {callback, makeCb()};
  callbacks[{"topic", 3}] = {makeCb(), callback, makeCb()};
  const auto testee = makeTestee(callbacks);
  const auto record = makeRecord("topic", 2);

  // when
  testee->receive(record);

  // then
  // Record was not stored.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 2), -1);
  // All records stay.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), 2);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 2), 2);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 3), 3);
}

TEST_F(RecordDistributorTest, ShouldPassRecordToCallbacksAndKeepItIfNoneAccepts) {
  // given
  PartitionMap<RecordCbSharedPtr> callbacks;
  callbacks[{"topic", 0}] = {makeCb()};
  callbacks[{"topic", 1}] = {makeCb(), makeCb()};
  const auto testee = makeTestee(callbacks);
  const auto record = makeRecord("topic", 1);

  // when
  testee->receive(record);

  // then
  // Record was stored.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 1), 1);
  // No changes in callbacks.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), 2);
}

TEST_F(RecordDistributorTest, ShouldProcessCallbackAndRegisterItIfThereAreNoMessages) {
  // given
  const auto cb = makeCb();
  const TopicToPartitionsMap tp = {{"topic", {0, 1}}};
  EXPECT_CALL(*cb, interest).WillRepeatedly(Return(tp));

  const auto testee = makeTestee();

  // when
  testee->processCallback(cb);

  // then - Callback got registered.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), 1);
}

// Compared to previous tests, there were interesting records before.
TEST_F(RecordDistributorTest, ShouldRegisterCallbackAndPassItMatchingMessages) {
  // given
  const auto cb = makeCb();
  const TopicToPartitionsMap tp = {{"topic", {0, 1}}};
  EXPECT_CALL(*cb, interest).WillRepeatedly(Return(tp));
  EXPECT_CALL(*cb, receive(_)).Times(4).WillRepeatedly(Return(CallbackReply::AcceptedAndWantMore));

  PartitionMap<InboundRecordSharedPtr> records;
  records[{"topic", 0}] = {makeRecord("topic", 0), makeRecord("topic", 1)};
  records[{"topic", 1}] = {makeRecord("topic", 1), makeRecord("topic", 1)};
  const auto testee = makeTestee(records);

  // when
  testee->processCallback(cb);

  // then
  // Messages got consumed.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getRecordCountForTest("topic", 1), -1);
  // Callback got registered.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), 1);
}

TEST_F(RecordDistributorTest, ShouldNotRegisterCallbackIfItGotSatisfied) {
  // given
  const auto cb = makeCb();
  const TopicToPartitionsMap tp = {{"topic", {0, 1, 2}}};
  EXPECT_CALL(*cb, interest).WillRepeatedly(Return(tp));
  EXPECT_CALL(*cb, receive(_))
      .Times(3)
      .WillOnce(Return(CallbackReply::AcceptedAndWantMore))
      .WillOnce(Return(CallbackReply::AcceptedAndWantMore))
      .WillOnce(Return(CallbackReply::AcceptedAndFinished));

  PartitionMap<InboundRecordSharedPtr> records;
  records[{"topic", 0}] = {makeRecord("topic", 0)};
  records[{"topic", 1}] = {makeRecord("topic", 1), makeRecord("topic", 1), makeRecord("topic", 1)};
  records[{"topic", 2}] = {makeRecord("topic", 2), makeRecord("topic", 2)};
  const auto testee = makeTestee(records);

  // when
  testee->processCallback(cb);

  // then
  // Some messages got consumed.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getRecordCountForTest("topic", 1), 1);
  ASSERT_EQ(testee->getRecordCountForTest("topic", 2), 2);
  // Callback was not registered.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), -1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 2), -1);
}

// Very similar to previous one, but makes sure we clean up vectors in record map.
TEST_F(RecordDistributorTest, ShouldNotRegisterCallbackIfItGotSatisfiedWithLastRecordInPartition) {
  // given
  const auto cb = makeCb();
  const TopicToPartitionsMap tp = {{"topic", {0}}};
  EXPECT_CALL(*cb, interest).WillRepeatedly(Return(tp));
  EXPECT_CALL(*cb, receive(_))
      .Times(3)
      .WillOnce(Return(CallbackReply::AcceptedAndWantMore))
      .WillOnce(Return(CallbackReply::AcceptedAndWantMore))
      .WillOnce(Return(CallbackReply::AcceptedAndFinished));

  PartitionMap<InboundRecordSharedPtr> records;
  records[{"topic", 0}] = {makeRecord("topic", 0), makeRecord("topic", 0), makeRecord("topic", 0)};
  const auto testee = makeTestee(records);

  // when
  testee->processCallback(cb);

  // then
  // All messages got consumed.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 0), -1);
  // Callback was not registered.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), -1);
}

TEST_F(RecordDistributorTest, ShouldNotRegisterCallbackIfItRejectsRecords) {
  // given
  const auto cb = makeCb();
  const TopicToPartitionsMap tp = {{"topic", {0, 1, 2}}};
  EXPECT_CALL(*cb, interest).WillRepeatedly(Return(tp));
  EXPECT_CALL(*cb, receive(_))
      .Times(3)
      .WillOnce(Return(CallbackReply::AcceptedAndWantMore))
      .WillOnce(Return(CallbackReply::AcceptedAndWantMore))
      .WillOnce(Return(CallbackReply::Rejected)); // Callback cancelled / abandoned / timed out.

  PartitionMap<InboundRecordSharedPtr> records;
  records[{"topic", 0}] = {makeRecord("topic", 0)};
  records[{"topic", 1}] = {makeRecord("topic", 1), makeRecord("topic", 1), makeRecord("topic", 1)};
  records[{"topic", 2}] = {makeRecord("topic", 2), makeRecord("topic", 2)};
  const auto testee = makeTestee(records);

  // when
  testee->processCallback(cb);

  // then
  // Some messages got consumed.
  ASSERT_EQ(testee->getRecordCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getRecordCountForTest("topic", 1), 2);
  ASSERT_EQ(testee->getRecordCountForTest("topic", 2), 2);
  // Callback was not registered.
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), -1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 2), -1);
}

TEST_F(RecordDistributorTest, ShouldRemoveCallbackProperly) {
  // given
  const auto callback = makeCb();
  PartitionMap<RecordCbSharedPtr> callbacks;
  callbacks[{"topic", 0}] = {callback};
  callbacks[{"topic", 1}] = {callback, makeCb()};
  callbacks[{"topic", 2}] = {makeCb(), callback};
  callbacks[{"topic", 3}] = {makeCb(), callback, makeCb()};
  const auto testee = makeTestee(callbacks);

  // when
  testee->removeCallback(callback);

  // then
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 0), -1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 1), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 2), 1);
  ASSERT_EQ(testee->getCallbackCountForTest("topic", 3), 2);
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
