#pragma once

#include <map>

#include "envoy/thread/thread.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/kafka_types.h"
#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils.h"
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
 * Basically core of Fetch-handling business logic.
 */
class RecordDistributor : public InboundRecordProcessor {
public:
  // InboundRecordProcessor
  bool waitUntilInterest(const std::string& topic, const int32_t timeout_ms) const override;

  // InboundRecordProcessor
  void receive(InboundRecordSharedPtr message) override;
};

using RecordDistributorPtr = std::unique_ptr<RecordDistributor>;

/**
 * Injectable for tests.
 */
class KafkaConsumerFactory {
public:
  virtual ~KafkaConsumerFactory() = default;

  // Create a Kafka consumer.
  virtual KafkaConsumerPtr createConsumer(InboundRecordProcessor& record_processor,
                                          Thread::ThreadFactory& thread_factory,
                                          const std::string& topic, const int32_t partition_count,
                                          const RawKafkaConfig& configuration) const PURE;
};

/**
 * Maintains a collection of Kafka consumers (one per topic).
 */
class SharedConsumerManagerImpl : public SharedConsumerManager,
                                  private Logger::Loggable<Logger::Id::kafka> {
public:
  // Main constructor.
  SharedConsumerManagerImpl(const UpstreamKafkaConfiguration& configuration,
                            Thread::ThreadFactory& thread_factory);

  // Visible for testing.
  SharedConsumerManagerImpl(const UpstreamKafkaConfiguration& configuration,
                            Thread::ThreadFactory& thread_factory,
                            const KafkaConsumerFactory& consumer_factory);

  // SharedConsumerManager
  void registerConsumerIfAbsent(const std::string& topic) override;

  size_t getConsumerCountForTest() const;

private:
  // Mutates 'topic_to_consumer_'.
  void registerNewConsumer(const std::string& topic)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(consumers_mutex_);

  RecordDistributorPtr distributor_;

  const UpstreamKafkaConfiguration& configuration_;
  Thread::ThreadFactory& thread_factory_;
  const KafkaConsumerFactory& consumer_factory_;

  mutable absl::Mutex consumers_mutex_;
  std::map<std::string, KafkaConsumerPtr> topic_to_consumer_ ABSL_GUARDED_BY(consumers_mutex_);
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
