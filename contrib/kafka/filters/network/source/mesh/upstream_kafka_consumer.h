#pragma once

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
