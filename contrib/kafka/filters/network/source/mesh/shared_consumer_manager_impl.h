#pragma once

#include <map>

#include "envoy/thread/thread.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/kafka_types.h"
#include "contrib/kafka/filters/network/source/mesh/shared_consumer_manager.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_config.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Placeholder interface for now.
 * In future:
 * Processor implementation that stores received records (that had no interest), and callbacks
 * waiting for records (that had no matching records delivered yet).
 */
class SharedProcessor : public InboundRecordProcessor {
public:
  // InboundRecordProcessor
  bool waitUntilInterest(const std::string& topic, const int32_t timeout_ms) const override;

  // InboundRecordProcessor
  void receive(InboundRecordSharedPtr message) override;
};

using SharedProcessorPtr = std::unique_ptr<SharedProcessor>;

/**
 * Implements SCM interface by maintaining a collection of Kafka consumers (one per topic).
 */
class SharedConsumerManagerImpl : public SharedConsumerManager,
                                  private Logger::Loggable<Logger::Id::kafka> {
public:
  SharedConsumerManagerImpl(const UpstreamKafkaConfiguration& configuration,
                            Thread::ThreadFactory& thread_factory);

  // SharedConsumerManager
  void registerConsumerIfAbsent(const std::string& topic) override;

private:
  // Mutates 'topic_to_consumer_'.
  void registerNewConsumer(const std::string& topic)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(consumers_mutex_);

  SharedProcessorPtr record_processor_;

  const UpstreamKafkaConfiguration& configuration_;
  Thread::ThreadFactory& thread_factory_;

  mutable absl::Mutex consumers_mutex_;
  std::map<std::string, KafkaConsumerPtr> topic_to_consumer_ ABSL_GUARDED_BY(consumers_mutex_);
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
