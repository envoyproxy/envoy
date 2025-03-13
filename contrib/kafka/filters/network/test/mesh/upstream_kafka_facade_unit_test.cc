#include "envoy/thread/thread.h"
#include "envoy/thread_local/thread_local.h"

#include "test/mocks/thread/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_facade.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockUpstreamKafkaConfiguration : public UpstreamKafkaConfiguration {
public:
  MOCK_METHOD(absl::optional<ClusterConfig>, computeClusterConfigForTopic, (const std::string&),
              (const));
  MOCK_METHOD((std::pair<std::string, int32_t>), getAdvertisedAddress, (), (const));
};

TEST(UpstreamKafkaFacadeTest, shouldCreateProducerOnlyOnceForTheSameCluster) {
  // given
  const std::string topic1 = "topic1";
  const std::string topic2 = "topic2";

  MockUpstreamKafkaConfiguration configuration;
  const ClusterConfig cluster_config = {
      "cluster", 1, {{"bootstrap.servers", "localhost:9092"}}, {}};
  EXPECT_CALL(configuration, computeClusterConfigForTopic(topic1)).WillOnce(Return(cluster_config));
  EXPECT_CALL(configuration, computeClusterConfigForTopic(topic2)).WillOnce(Return(cluster_config));
  ThreadLocal::MockInstance slot_allocator;
  EXPECT_CALL(slot_allocator, allocateSlot())
      .WillOnce(Invoke(&slot_allocator, &ThreadLocal::MockInstance::allocateSlotMock));
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  UpstreamKafkaFacadeImpl testee = {configuration, slot_allocator, thread_factory};

  // when
  auto& result1 = testee.getProducerForTopic(topic1);
  auto& result2 = testee.getProducerForTopic(topic2);

  // then
  EXPECT_EQ(&result1, &result2);
  EXPECT_EQ(testee.getProducerCountForTest(), 1);
}

TEST(UpstreamKafkaFacadeTest, shouldCreateDifferentProducersForDifferentClusters) {
  // given
  const std::string topic1 = "topic1";
  const std::string topic2 = "topic2";

  MockUpstreamKafkaConfiguration configuration;
  // Notice it's the cluster name that matters, not the producer config.
  const ClusterConfig cluster_config1 = {
      "cluster1", 1, {{"bootstrap.servers", "localhost:9092"}}, {}};
  EXPECT_CALL(configuration, computeClusterConfigForTopic(topic1))
      .WillOnce(Return(cluster_config1));
  const ClusterConfig cluster_config2 = {
      "cluster2", 1, {{"bootstrap.servers", "localhost:9092"}}, {}};
  EXPECT_CALL(configuration, computeClusterConfigForTopic(topic2))
      .WillOnce(Return(cluster_config2));
  ThreadLocal::MockInstance slot_allocator;
  EXPECT_CALL(slot_allocator, allocateSlot())
      .WillOnce(Invoke(&slot_allocator, &ThreadLocal::MockInstance::allocateSlotMock));
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  UpstreamKafkaFacadeImpl testee = {configuration, slot_allocator, thread_factory};

  // when
  auto& result1 = testee.getProducerForTopic(topic1);
  auto& result2 = testee.getProducerForTopic(topic2);

  // then
  EXPECT_NE(&result1, &result2);
  EXPECT_EQ(testee.getProducerCountForTest(), 2);
}

TEST(UpstreamKafkaFacadeTest, shouldThrowIfThereIsNoConfigurationForGivenTopic) {
  // given
  const std::string topic = "topic1";

  MockUpstreamKafkaConfiguration configuration;
  const ClusterConfig cluster_config = {
      "cluster", 1, {{"bootstrap.servers", "localhost:9092"}}, {}};
  EXPECT_CALL(configuration, computeClusterConfigForTopic(topic)).WillOnce(Return(absl::nullopt));
  ThreadLocal::MockInstance slot_allocator;
  EXPECT_CALL(slot_allocator, allocateSlot())
      .WillOnce(Invoke(&slot_allocator, &ThreadLocal::MockInstance::allocateSlotMock));
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  UpstreamKafkaFacadeImpl testee = {configuration, slot_allocator, thread_factory};

  // when, then - exception gets thrown.
  EXPECT_THROW(testee.getProducerForTopic(topic), EnvoyException);
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
