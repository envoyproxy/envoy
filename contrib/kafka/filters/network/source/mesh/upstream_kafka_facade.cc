#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_facade.h"

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client_impl.h"

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
  ThreadLocalKafkaFacade(const UpstreamKafkaConfiguration& configuration,
                         Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory);
  ~ThreadLocalKafkaFacade() override;

  KafkaProducer& getProducerForTopic(const std::string& topic);

  size_t getProducerCountForTest() const;

private:
  // Mutates 'cluster_to_kafka_client_'.
  KafkaProducer& registerNewProducer(const ClusterConfig& cluster_config);

  const UpstreamKafkaConfiguration& configuration_;
  Event::Dispatcher& dispatcher_;
  Thread::ThreadFactory& thread_factory_;

  std::map<std::string, KafkaProducerPtr> cluster_to_kafka_client_;
};

ThreadLocalKafkaFacade::ThreadLocalKafkaFacade(const UpstreamKafkaConfiguration& configuration,
                                               Event::Dispatcher& dispatcher,
                                               Thread::ThreadFactory& thread_factory)
    : configuration_{configuration}, dispatcher_{dispatcher}, thread_factory_{thread_factory} {}

ThreadLocalKafkaFacade::~ThreadLocalKafkaFacade() {
  // Because the producers take a moment to shutdown, we mark their monitoring threads as shut down
  // before the destructors get called.
  for (auto& entry : cluster_to_kafka_client_) {
    entry.second->markFinished();
  }
}

KafkaProducer& ThreadLocalKafkaFacade::getProducerForTopic(const std::string& topic) {
  const absl::optional<ClusterConfig> cluster_config =
      configuration_.computeClusterConfigForTopic(topic);
  if (cluster_config) {
    const auto it = cluster_to_kafka_client_.find(cluster_config->name_);
    // Return client already present or create new one and register it.
    return (cluster_to_kafka_client_.end() == it) ? registerNewProducer(*cluster_config)
                                                  : *(it->second);
  } else {
    throw EnvoyException(absl::StrCat("cannot compute target producer for topic: ", topic));
  }
}

KafkaProducer& ThreadLocalKafkaFacade::registerNewProducer(const ClusterConfig& cluster_config) {
  ENVOY_LOG(debug, "Registering new Kafka producer for cluster [{}]", cluster_config.name_);
  KafkaProducerPtr new_producer = std::make_unique<RichKafkaProducer>(
      dispatcher_, thread_factory_, cluster_config.upstream_producer_properties_);
  auto result = cluster_to_kafka_client_.emplace(cluster_config.name_, std::move(new_producer));
  return *(result.first->second);
}

size_t ThreadLocalKafkaFacade::getProducerCountForTest() const {
  return cluster_to_kafka_client_.size();
}

UpstreamKafkaFacadeImpl::UpstreamKafkaFacadeImpl(const UpstreamKafkaConfiguration& configuration,
                                                 ThreadLocal::SlotAllocator& slot_allocator,
                                                 Thread::ThreadFactory& thread_factory)
    : tls_{slot_allocator.allocateSlot()} {

  ThreadLocal::Slot::InitializeCb cb =
      [&configuration,
       &thread_factory](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalKafkaFacade>(configuration, dispatcher, thread_factory);
  };
  tls_->set(cb);
}

// Return KafkaProducer instance that is local to given thread, via ThreadLocalKafkaFacade.
KafkaProducer& UpstreamKafkaFacadeImpl::getProducerForTopic(const std::string& topic) {
  return tls_->getTyped<ThreadLocalKafkaFacade>().getProducerForTopic(topic);
}

size_t UpstreamKafkaFacadeImpl::getProducerCountForTest() const {
  return tls_->getTyped<ThreadLocalKafkaFacade>().getProducerCountForTest();
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
