#pragma once

#include "envoy/thread/thread.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/mesh/clustering.h"
#include "extensions/filters/network/kafka/mesh/upstream_kafka_client.h"

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
  virtual ~UpstreamKafkaFacade(){};
  virtual KafkaProducerWrapper& getProducerForTopic(const std::string& topic) PURE;
};

using UpstreamKafkaFacadeSharedPtr = std::shared_ptr<UpstreamKafkaFacade>;

/**
 * Provides access to upstream Kafka clients.
 * This is done by using thread-local maps of cluster to producer.
 */
class UpstreamKafkaFacadeImpl : public UpstreamKafkaFacade,
                                private Logger::Loggable<Logger::Id::kafka> {
public:
  UpstreamKafkaFacadeImpl(const ClusteringConfiguration& clustering_configuration,
                          ThreadLocal::SlotAllocator& slot_allocator,
                          Thread::ThreadFactory& thread_factory);

  // WRITE DOC
  KafkaProducerWrapper& getProducerForTopic(const std::string& topic) override;

  size_t getProducerCountForTest();

private:
  ThreadLocal::SlotPtr tls_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
