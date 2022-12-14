#include "envoy/common/exception.h"
#include "envoy/thread/thread.h"

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
using testing::Return;

class MockThreadFactory : public Thread::ThreadFactory {
public:
  MOCK_METHOD(Thread::ThreadPtr, createThread, (std::function<void()>, Thread::OptionsOptConstRef));
  MOCK_METHOD(Thread::ThreadId, currentThreadId, ());
};

class MockUpstreamKafkaConfiguration : public UpstreamKafkaConfiguration {
public:
  MOCK_METHOD(absl::optional<ClusterConfig>, computeClusterConfigForTopic, (const std::string&),
              (const));
  MOCK_METHOD((std::pair<std::string, int32_t>), getAdvertisedAddress, (), (const));
};

class MockInboundRecordProcessor : public InboundRecordProcessor {
public:
  MOCK_METHOD(void, receive, (InboundRecordSharedPtr), ());
  MOCK_METHOD(bool, waitUntilInterest, (const std::string&, const int32_t), (const));
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
  MockThreadFactory thread_factory_;
  MockUpstreamKafkaConfiguration configuration_;
  MockKafkaConsumerFactory consumer_factory_;
  MockInboundRecordProcessor processor_;

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
    testee->registerConsumerIfAbsent(topic1, processor_);
  }
  for (int i = 0; i < 3; ++i) {
    testee->registerConsumerIfAbsent(topic2, processor_);
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
  EXPECT_THROW(testee->registerConsumerIfAbsent(topic, processor_), EnvoyException);
  ASSERT_EQ(testee->getConsumerCountForTest(), 0);
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
