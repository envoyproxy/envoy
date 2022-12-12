#pragma once

#include <atomic>
#include <list>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/thread/thread.h"

#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Combines the librdkafka consumer and its dedicated worker thread.
 * The thread receives the records, and pushes them to the processor.
 * The consumer is going to receive the records from all the partitions for the given topic.
 */
class RichKafkaConsumer : public KafkaConsumer, private Logger::Loggable<Logger::Id::kafka> {
public:
  // Main constructor.
  RichKafkaConsumer(InboundRecordProcessor& record_processor, Thread::ThreadFactory& thread_factory,
                    const std::string& topic, const int32_t partition_count,
                    const RawKafkaConfig& configuration);

  // Visible for testing (allows injection of LibRdKafkaUtils).
  RichKafkaConsumer(InboundRecordProcessor& record_processor, Thread::ThreadFactory& thread_factory,
                    const std::string& topic, const int32_t partition_count,
                    const RawKafkaConfig& configuration, const LibRdKafkaUtils& utils);

  // More complex than usual - closes the real Kafka consumer and disposes of the assignment object.
  ~RichKafkaConsumer() override;

private:
  // This method continuously fetches new records and passes them to processor.
  // Does not finish until this object gets destroyed.
  // Executed in the dedicated worker thread.
  void runWorkerLoop();

  // Uses internal consumer to receive records from upstream.
  std::vector<InboundRecordSharedPtr> receiveRecordBatch();

  // The record processor (provides info whether it wants records and consumes them).
  InboundRecordProcessor& record_processor_;

  // The topic we are consuming from.
  std::string topic_;

  // Real Kafka consumer (NOT thread-safe).
  // All access to this thing happens in the worker thread.
  std::unique_ptr<RdKafka::KafkaConsumer> consumer_;

  // Consumer's partition assignment.
  ConsumerAssignmentConstPtr assignment_;

  // Flag controlling worker threads's execution.
  std::atomic<bool> worker_thread_active_;

  // Real worker thread.
  // Responsible for getting records from upstream Kafka with a consumer and passing these records
  // to the processor.
  Thread::ThreadPtr worker_thread_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
