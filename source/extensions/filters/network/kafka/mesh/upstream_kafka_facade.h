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
 * Provides thread-safe access to upstream Kafka clients.
 */
class UpstreamKafkaFacade : private Logger::Loggable<Logger::Id::kafka> {
public:
  UpstreamKafkaFacade(const ClusteringConfiguration& clustering_configuration,
                      ThreadLocal::SlotAllocator& slot_allocator,
                      Thread::ThreadFactory& thread_factory);

  KafkaProducerWrapper& getProducerForTopic(const std::string& topic);

private:
  ThreadLocal::SlotPtr tls_;
};

using UpstreamKafkaFacadeSharedPtr = std::shared_ptr<UpstreamKafkaFacade>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
