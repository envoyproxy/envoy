#include "extensions/filters/network/kafka/mesh/upstream_kafka_facade.h"

#include <thread>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Responsible for keeping a map of upstream-facing Kafka clients.
 */
class ThreadLocalKafkaFacade : public ThreadLocal::ThreadLocalObject,
                               private Logger::Loggable<Logger::Id::kafka> {
public:
  ThreadLocalKafkaFacade(const ClusteringConfiguration& clustering_configuration,
                         Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory);
  ~ThreadLocalKafkaFacade() override;

  KafkaProducerWrapper& getProducerForTopic(const std::string& topic);

  size_t getProducerCountForTest() const;

private:
  // Mutates 'cluster_to_kafka_client_'.
  KafkaProducerWrapper& registerNewProducer(const ClusterConfig& cluster_config);

  const ClusteringConfiguration& clustering_configuration_;
  Event::Dispatcher& dispatcher_;
  Thread::ThreadFactory& thread_factory_;

  std::map<std::string, KafkaProducerWrapperPtr> cluster_to_kafka_client_;
};

ThreadLocalKafkaFacade::ThreadLocalKafkaFacade(
    const ClusteringConfiguration& clustering_configuration, Event::Dispatcher& dispatcher,
    Thread::ThreadFactory& thread_factory)
    : clustering_configuration_{clustering_configuration}, dispatcher_{dispatcher},
      thread_factory_{thread_factory} {}

ThreadLocalKafkaFacade::~ThreadLocalKafkaFacade() {
  for (auto& entry : cluster_to_kafka_client_) {
    entry.second->markFinished();
  }
}

KafkaProducerWrapper& ThreadLocalKafkaFacade::getProducerForTopic(const std::string& topic) {
  const absl::optional<ClusterConfig> cluster_config =
      clustering_configuration_.computeClusterConfigForTopic(topic);
  if (cluster_config) {
    const auto it = cluster_to_kafka_client_.find(cluster_config->name_);
    // Return client already present or create new one and register it.
    return (cluster_to_kafka_client_.end() == it) ? registerNewProducer(*cluster_config)
                                                  : *(it->second);
  } else {
    throw EnvoyException(absl::StrCat("cannot compute target producer for topic: ", topic));
  }
}

KafkaProducerWrapper&
ThreadLocalKafkaFacade::registerNewProducer(const ClusterConfig& cluster_config) {
  ENVOY_LOG(info, "Register new Kafka producer for cluster [{}]", cluster_config.name_);
  KafkaProducerWrapperPtr new_producer = std::make_unique<KafkaProducerWrapper>(
      dispatcher_, thread_factory_, cluster_config.upstream_producer_properties_);
  auto result = cluster_to_kafka_client_.emplace(cluster_config.name_, std::move(new_producer));
  return *(result.first->second);
}

size_t ThreadLocalKafkaFacade::getProducerCountForTest() const {
  return cluster_to_kafka_client_.size();
}

UpstreamKafkaFacadeImpl::UpstreamKafkaFacadeImpl(
    const ClusteringConfiguration& clustering_configuration,
    ThreadLocal::SlotAllocator& slot_allocator, Thread::ThreadFactory& thread_factory)
    : tls_{slot_allocator.allocateSlot()} {

  ThreadLocal::Slot::InitializeCb cb =
      [&clustering_configuration,
       &thread_factory](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalKafkaFacade>(clustering_configuration, dispatcher,
                                                    thread_factory);
  };
  tls_->set(cb);
}

// Return Producer instance that is local to given thread, via ThreadLocalKafkaFacade.
KafkaProducerWrapper& UpstreamKafkaFacadeImpl::getProducerForTopic(const std::string& topic) {
  return tls_->getTyped<ThreadLocalKafkaFacade>().getProducerForTopic(topic);
}

size_t UpstreamKafkaFacadeImpl::getProducerCountForTest() {
  return tls_->getTyped<ThreadLocalKafkaFacade>().getProducerCountForTest();
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
