#pragma once

#include <map>
#include <memory>
#include <utility>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "contrib/kafka/filters/network/source/mesh/outbound_record.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Trivial memento that keeps the information about how given request was delivered:
// in case of success this means offset (if acks > 0), or error code.
struct DeliveryMemento {

  // Pointer to byte array that was passed to Kafka producer.
  // We use this to tell apart messages.
  // Important: we do not free this memory, it's still part of the 'ProduceRequestHandler' object.
  // Future work: adopt Kafka's opaque-pointer functionality so we use less memory instead of
  // keeping whole payload until we receive a confirmation.
  const void* data_;

  // Kafka producer error code.
  const int32_t error_code_;

  // Offset (only meaningful if error code is equal to 0).
  const int64_t offset_;
};

// Callback for objects that want to be notified that record delivery has been finished.
class ProduceFinishCb {
public:
  virtual ~ProduceFinishCb() = default;

  // Attempt to process this delivery.
  // @returns true if given callback is related to this delivery
  virtual bool accept(const DeliveryMemento& memento) PURE;
};

using ProduceFinishCbSharedPtr = std::shared_ptr<ProduceFinishCb>;

/**
 * Filter facing interface.
 * A thing that takes records and sends them to upstream Kafka.
 */
class KafkaProducer {
public:
  virtual ~KafkaProducer() = default;

  /*
   * Sends given record (key, value) to Kafka (topic, partition).
   * When delivery is finished, it notifies the callback provided with corresponding delivery data
   * (error code, offset).
   *
   * @param origin origin of payload to be notified when delivery finishes.
   * @param record record data to be sent.
   */
  virtual void send(const ProduceFinishCbSharedPtr origin, const OutboundRecord& record) PURE;

  // Impl leakage: real implementations of Kafka Producer need to stop a monitoring thread, then
  // they can close the producer. Because the polling thread should not be interrupted, we just mark
  // it as finished, and it's going to notice that change on the next iteration.
  // Theoretically we do not need to do this and leave it all to destructor, but then closing N
  // producers would require doing that in sequence, while we can optimize it somewhat (so we just
  // wait for the slowest one).
  // See https://github.com/confluentinc/librdkafka/issues/2972
  virtual void markFinished() PURE;
};

using KafkaProducerPtr = std::unique_ptr<KafkaProducer>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
