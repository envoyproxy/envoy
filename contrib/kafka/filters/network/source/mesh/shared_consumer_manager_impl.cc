#include "contrib/kafka/filters/network/source/mesh/shared_consumer_manager_impl.h"

#include "source/common/common/fmt.h"

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// KafkaConsumerFactoryImpl

class KafkaConsumerFactoryImpl : public KafkaConsumerFactory {
public:
  // KafkaConsumerFactory
  KafkaConsumerPtr createConsumer(InboundRecordProcessor& record_processor,
                                  Thread::ThreadFactory& thread_factory, const std::string& topic,
                                  const int32_t partition_count,
                                  const RawKafkaConfig& configuration) const override;

  // Default singleton accessor.
  static const KafkaConsumerFactory& getDefaultInstance();
};

KafkaConsumerPtr
KafkaConsumerFactoryImpl::createConsumer(InboundRecordProcessor& record_processor,
                                         Thread::ThreadFactory& thread_factory,
                                         const std::string& topic, const int32_t partition_count,
                                         const RawKafkaConfig& configuration) const {
  return std::make_unique<RichKafkaConsumer>(record_processor, thread_factory, topic,
                                             partition_count, configuration);
}

const KafkaConsumerFactory& KafkaConsumerFactoryImpl::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(KafkaConsumerFactoryImpl);
}

// SharedConsumerManagerImpl

SharedConsumerManagerImpl::SharedConsumerManagerImpl(
    const UpstreamKafkaConfiguration& configuration, Thread::ThreadFactory& thread_factory)
    : SharedConsumerManagerImpl{configuration, thread_factory,
                                KafkaConsumerFactoryImpl::getDefaultInstance()} {}

SharedConsumerManagerImpl::SharedConsumerManagerImpl(
    const UpstreamKafkaConfiguration& configuration, Thread::ThreadFactory& thread_factory,
    const KafkaConsumerFactory& consumer_factory)
    : distributor_{std::make_unique<RecordDistributor>()}, configuration_{configuration},
      thread_factory_{thread_factory}, consumer_factory_{consumer_factory} {}

void SharedConsumerManagerImpl::registerConsumerIfAbsent(const std::string& topic) {
  absl::MutexLock lock(&consumers_mutex_);
  const auto it = topic_to_consumer_.find(topic);
  // Return consumer already present or create new one and register it.
  if (topic_to_consumer_.end() == it) {
    registerNewConsumer(topic);
  }
}

void SharedConsumerManagerImpl::registerNewConsumer(const std::string& topic) {
  ENVOY_LOG(debug, "Creating consumer for topic [{}]", topic);

  // Compute which upstream cluster corresponds to the topic.
  const absl::optional<ClusterConfig> cluster_config =
      configuration_.computeClusterConfigForTopic(topic);
  if (!cluster_config) {
    throw EnvoyException(
        fmt::format("Could not compute upstream cluster configuration for topic [{}]", topic));
  }

  // Create the consumer and register it.
  KafkaConsumerPtr new_consumer = consumer_factory_.createConsumer(
      *distributor_, thread_factory_, topic, cluster_config->partition_count_,
      cluster_config->upstream_consumer_properties_);
  ENVOY_LOG(debug, "Registering new Kafka consumer for topic [{}], consuming from cluster [{}]",
            topic, cluster_config->name_);
  topic_to_consumer_.emplace(topic, std::move(new_consumer));
}

size_t SharedConsumerManagerImpl::getConsumerCountForTest() const {
  absl::MutexLock lock(&consumers_mutex_);
  return topic_to_consumer_.size();
}

// RecordDistributor

bool RecordDistributor::waitUntilInterest(const std::string&, const int32_t) const {
  // TODO (adam.kotwasinski) To implement in future commits.
  return false;
}

void RecordDistributor::receive(InboundRecordSharedPtr) {
  // TODO (adam.kotwasinski) To implement in future commits.
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
