#pragma once

#include <deque>
#include <thread>

#include "envoy/event/dispatcher.h"

#include "common/common/logger.h"

#include "librdkafka/rdkafkacpp.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Trivial memento that keeps the information about how given request was delivered:
// in case of success this means offset (if acks > 0), or error code.
struct DeliveryMemento {

  // Pointer to byte array that was passed to Kafka producer.
  // This is used to find the original record sent upstream.
  // Important: we do not free this memory, it's still part of the 'Request' object two levels
  // above.
  const void* data_;

  // Kafka producer error code.
  const RdKafka::ErrorCode error_code_;

  // Offset (only meaningful if error code is equal to 0).
  const int64_t offset_;
};

class ProduceFinishCb {
public:
  virtual ~ProduceFinishCb() = default;

  virtual bool accept(const DeliveryMemento& memento) PURE;
};

using ProduceFinishCbSharedPtr = std::shared_ptr<ProduceFinishCb>;

using RawKafkaProducerConfig = std::map<std::string, std::string>;

/**
 * Combines the librdkafka producer and its monitoring thread.
 * Producer is used to schedule messages to be sent to Kafka.
 * Independently running monitoring thread picks up delivery confirmations from producer and uses
 * Dispatcher to notify itself about delivery in worker thread.
 */
class KafkaProducerWrapper : public RdKafka::DeliveryReportCb,
                             private Logger::Loggable<Logger::Id::kafka> {
public:
  KafkaProducerWrapper(Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory,
                       const RawKafkaProducerConfig& configuration);

  /**
   * More complex than usual.
   * Marks that monitoring thread should finish and waits for it to join.
   */
  ~KafkaProducerWrapper() override;

  /**
   * Submits given payload to be sent to given topic:partition.
   * If successful, after successful delivery callback method will be invoked sometime in future by
   * monitoring thread. If there's a produce failure, we are going to notify the callback
   * immediately with failure data.
   *
   * @param origin origin of payload to be notified when delivery finishes.
   * @param topic Kafka topic.
   * @param partition Kafka partition (as clients do partitioning, we just reuse what downstream
   * gave us).
   * @param key Kafka message key.
   * @param value Kafka message value.
   */
  void send(const ProduceFinishCbSharedPtr origin, const std::string& topic,
            const int32_t partition, const absl::string_view key, const absl::string_view value);

  void processDelivery(const DeliveryMemento& memento);

  void checkDeliveryReports();

  // RdKafka::DeliveryReportCb
  void dr_cb(RdKafka::Message& message) override;

  void markFinished();

private:
  ProduceFinishCbSharedPtr getMatching(const RdKafka::Message& message);

  Event::Dispatcher& dispatcher_;

  std::list<ProduceFinishCbSharedPtr> unfinished_produce_requests_;

  // Real Kafka producer (thread-safe).
  // Invoked by Envoy handler thread (to produce), and internal monitoring thread
  // (to poll for delivery events).
  std::unique_ptr<RdKafka::Producer> producer_;

  // Flag controlling monitoring threads's execution.
  volatile bool poller_thread_active_;

  // Monitoring thread that's responsible for continuously polling for new Kafka producer events.
  Thread::ThreadPtr poller_thread_;
};

using KafkaProducerWrapperPtr = std::unique_ptr<KafkaProducerWrapper>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
