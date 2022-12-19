#pragma once

#include <map>
#include <vector>

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

template <typename T> using PartitionMap = std::map<KafkaPartition, std::vector<T>>;

/**
 * Processor implementation that stores received records (that had no interest),
 * and callbacks waiting for records (that had no matching records delivered yet).
 * Basically core of Fetch-handling business logic.
 */
class RecordDistributor : public RecordCallbackProcessor,
                          public InboundRecordProcessor,
                          private Logger::Loggable<Logger::Id::kafka> {
public:
  // Main constructor.
  RecordDistributor();

  // Visible for testing.
  RecordDistributor(const PartitionMap<RecordCbSharedPtr>& callbacks,
                    const PartitionMap<InboundRecordSharedPtr>& records);

  // InboundRecordProcessor
  bool waitUntilInterest(const std::string& topic, const int32_t timeout_ms) const override;

  // InboundRecordProcessor
  void receive(InboundRecordSharedPtr message) override;

  // RecordCallbackProcessor
  void processCallback(const RecordCbSharedPtr& callback) override;

  // RecordCallbackProcessor
  void removeCallback(const RecordCbSharedPtr& callback) override;

  int32_t getCallbackCountForTest(const std::string& topic, const int32_t partition) const;

  int32_t getRecordCountForTest(const std::string& topic, const int32_t partition) const;

private:
  // Checks whether any of the callbacks stored right now are interested in the topic.
  bool hasInterest(const std::string& topic) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(callbacks_mutex_);

  // Helper function (passes all stored records to callback).
  bool passRecordsToCallback(const RecordCbSharedPtr& callback);

  // Helper function (passes partition records to callback).
  bool passPartitionRecordsToCallback(const RecordCbSharedPtr& callback,
                                      const KafkaPartition& kafka_partition)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(stored_records_mutex_);

  // Helper function (real callback removal).
  void doRemoveCallback(const RecordCbSharedPtr& callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(callbacks_mutex_);

  /**
   * Invariant - for every KafkaPartition, there may be callbacks for this partition,
   * or there may be records for this partition, but never both at the same time.
   */

  mutable absl::Mutex callbacks_mutex_;
  PartitionMap<RecordCbSharedPtr> partition_to_callbacks_ ABSL_GUARDED_BY(callbacks_mutex_);

  mutable absl::Mutex stored_records_mutex_;
  PartitionMap<InboundRecordSharedPtr> stored_records_ ABSL_GUARDED_BY(stored_records_mutex_);
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
 * Maintains a collection of Kafka consumers (one per topic) and the real distributor instance.
 */
class SharedConsumerManagerImpl : public RecordCallbackProcessor,
                                  public SharedConsumerManager,
                                  private Logger::Loggable<Logger::Id::kafka> {
public:
  // Main constructor.
  SharedConsumerManagerImpl(const UpstreamKafkaConfiguration& configuration,
                            Thread::ThreadFactory& thread_factory);

  // Visible for testing.
  SharedConsumerManagerImpl(const UpstreamKafkaConfiguration& configuration,
                            Thread::ThreadFactory& thread_factory,
                            const KafkaConsumerFactory& consumer_factory);

  // RecordCallbackProcessor
  void processCallback(const RecordCbSharedPtr& callback) override;

  // RecordCallbackProcessor
  void removeCallback(const RecordCbSharedPtr& callback) override;

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
