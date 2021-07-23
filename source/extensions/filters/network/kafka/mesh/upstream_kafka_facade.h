#pragma once

#include "envoy/thread/thread.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/kafka/mesh/upstream_config.h"
#include "source/extensions/filters/network/kafka/mesh/upstream_kafka_client.h"

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
  virtual RecordSink& getProducerForTopic(const std::string& topic) PURE;
};

using UpstreamKafkaFacadeSharedPtr = std::shared_ptr<UpstreamKafkaFacade>;

/**
 * Provides access to upstream Kafka clients.
 * This is done by using thread-local maps of cluster to producer.
 */
class UpstreamKafkaFacadeImpl : public UpstreamKafkaFacade,
                                private Logger::Loggable<Logger::Id::kafka> {
public:
  UpstreamKafkaFacadeImpl(const UpstreamKafkaConfiguration& configuration,
                          ThreadLocal::SlotAllocator& slot_allocator,
                          Thread::ThreadFactory& thread_factory);

  // Returns a Kafka producer that points to a cluster for a given topic.
  RecordSink& getProducerForTopic(const std::string& topic) override;

  size_t getProducerCountForTest();

private:
  ThreadLocal::SlotPtr tls_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
