#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"

#include "contrib/kafka/filters/network/source/mesh/inbound_record.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Topic name to topic partitions.
using TopicToPartitionsMap = std::map<std::string, std::vector<int32_t>>;

/**
 * Callback's response when it is provided with a record.
 */
enum class CallbackReply {
  // Callback is not interested in any record anymore.
  // Impl note: this can be caused by callback timing out in between.
  Rejected,
  // Callback consumed the record and is capable of accepting more.
  AcceptedAndWantMore,
  // Callback consumed the record and will not receive more (allows us to optimize a little).
  AcceptedAndFinished,
};

/**
 * Callback for objects that want to be notified that new Kafka record has been received.
 */
class RecordCb {
public:
  virtual ~RecordCb() = default;

  /**
   * Notify the callback with a record.
   * @return whether the callback could accept the message
   */
  virtual CallbackReply receive(InboundRecordSharedPtr record) PURE;

  /**
   * What partitions this callback is interested in.
   */
  virtual TopicToPartitionsMap interest() const PURE;

  /**
   * Pretty string identifier of given callback (i.e. downstream request). Used in logging.
   */
  virtual std::string toString() const PURE;
};

using RecordCbSharedPtr = std::shared_ptr<RecordCb>;

/**
 * An entity that is interested in inbound records delivered by Kafka consumer.
 */
class InboundRecordProcessor {
public:
  virtual ~InboundRecordProcessor() = default;

  /**
   * Passes the record to the processor.
   */
  virtual void receive(InboundRecordSharedPtr message) PURE;

  /**
   * Blocks until there is interest in records in a given topic, or timeout expires.
   * Conceptually a thick condition variable.
   * @return true if there was interest.
   */
  virtual bool waitUntilInterest(const std::string& topic, const int32_t timeout_ms) const PURE;
};

using InboundRecordProcessorPtr = std::unique_ptr<InboundRecordProcessor>;

/**
 * Kafka consumer pointing to some upstream Kafka cluster.
 */
class KafkaConsumer {
public:
  virtual ~KafkaConsumer() = default;
};

using KafkaConsumerPtr = std::unique_ptr<KafkaConsumer>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
