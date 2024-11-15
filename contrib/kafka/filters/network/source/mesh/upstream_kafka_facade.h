#pragma once

#include "envoy/thread/thread.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/mesh/upstream_config.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Provides access to upstream Kafka clients.
 */
class UpstreamKafkaFacade {
public:
  virtual ~UpstreamKafkaFacade() = default;

  /**
   * Returns a Kafka producer that points an upstream Kafka cluster that is supposed to receive
   * messages for the given topic.
   */
  virtual KafkaProducer& getProducerForTopic(const std::string& topic) PURE;
};

using UpstreamKafkaFacadeSharedPtr = std::shared_ptr<UpstreamKafkaFacade>;

/**
 * Provides access to upstream Kafka clients.
 * This is done by using thread-local maps of cluster to producer.
 * We are going to have one Kafka producer per upstream cluster, per Envoy worker thread.
 */
class UpstreamKafkaFacadeImpl : public UpstreamKafkaFacade,
                                private Logger::Loggable<Logger::Id::kafka> {
public:
  UpstreamKafkaFacadeImpl(const UpstreamKafkaConfiguration& configuration,
                          ThreadLocal::SlotAllocator& slot_allocator,
                          Thread::ThreadFactory& thread_factory);

  // UpstreamKafkaFacade
  KafkaProducer& getProducerForTopic(const std::string& topic) override;

  size_t getProducerCountForTest() const;

private:
  ThreadLocal::SlotPtr tls_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
