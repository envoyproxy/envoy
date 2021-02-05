#pragma once

#include <atomic>
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

// Callback for objects that want to be notified that record delivery has been finished.
class ProduceFinishCb {
public:
  virtual ~ProduceFinishCb() = default;

  virtual bool accept(const DeliveryMemento& memento) PURE;
};

using ProduceFinishCbSharedPtr = std::shared_ptr<ProduceFinishCb>;

using RawKafkaProducerConfig = std::map<std::string, std::string>;

/**
 * Helper class responsible for creating librdkafka entities, so we can have mocks in tests.
 */
class LibRdKafkaUtils {
public:
  virtual ~LibRdKafkaUtils() = default;
  virtual RdKafka::Conf::ConfResult setConfProperty(RdKafka::Conf& conf, const std::string& name,
                                                    const std::string& value,
                                                    std::string& errstr) const PURE;
  virtual RdKafka::Conf::ConfResult setConfDeliveryCallback(RdKafka::Conf& conf,
                                                            RdKafka::DeliveryReportCb* dr_cb,
                                                            std::string& errstr) const PURE;
  virtual std::unique_ptr<RdKafka::Producer> createProducer(RdKafka::Conf* conf,
                                                            std::string& errstr) const PURE;
};

/**
 * Filter facing interface.
 * A thing that takes records and sends them to upstream Kafka.
 * IMPL note: this interface has been extracted because users will not care how this thing works
 * (i.e. RichKafkaProducer callback etc.).
 */
class RecordSink {
public:
  virtual ~RecordSink() = default;

  virtual void send(const ProduceFinishCbSharedPtr origin, const std::string& topic,
                    const int32_t partition, const absl::string_view key,
                    const absl::string_view value) PURE;
};

/**
 * Combines the librdkafka producer and its monitoring thread.
 * Producer is used to schedule messages to be sent to Kafka.
 * Independently running monitoring thread picks up delivery confirmations from producer and uses
 * Dispatcher to notify itself about delivery in worker thread.
 */
class RichKafkaProducer : public RecordSink,
                          public RdKafka::DeliveryReportCb,
                          private Logger::Loggable<Logger::Id::kafka> {
public:
  // Usual constructor.
  RichKafkaProducer(Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory,
                    const RawKafkaProducerConfig& configuration);

  // Visible for testing (allows injection of LibRdKafkaUtils).
  RichKafkaProducer(Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory,
                    const RawKafkaProducerConfig& configuration, const LibRdKafkaUtils& utils);

  /**
   * More complex than usual.
   * Marks that monitoring thread should finish and waits for it to join.
   */
  ~RichKafkaProducer() override;

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
            const int32_t partition, const absl::string_view key,
            const absl::string_view value) override;

  void processDelivery(const DeliveryMemento& memento);

  void checkDeliveryReports();

  // RdKafka::DeliveryReportCb
  void dr_cb(RdKafka::Message& message) override;

  void markFinished();

  std::list<ProduceFinishCbSharedPtr>& getUnfinishedRequestsForTest();

private:
  ProduceFinishCbSharedPtr getMatching(const RdKafka::Message& message);

  Event::Dispatcher& dispatcher_;

  std::list<ProduceFinishCbSharedPtr> unfinished_produce_requests_;

  // Real Kafka producer (thread-safe).
  // Invoked by Envoy handler thread (to produce), and internal monitoring thread
  // (to poll for delivery events).
  std::unique_ptr<RdKafka::Producer> producer_;

  // Flag controlling monitoring threads's execution.
  std::atomic<bool> poller_thread_active_;

  // Monitoring thread that's responsible for continuously polling for new Kafka producer events.
  Thread::ThreadPtr poller_thread_;
};

using RichKafkaProducerPtr = std::unique_ptr<RichKafkaProducer>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
