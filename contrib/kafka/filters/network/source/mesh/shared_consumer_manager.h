#pragma once

#include <memory>
#include <string>

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Processes incoming record callbacks (i.e. Fetch requests).
 */
class RecordCallbackProcessor {
public:
  virtual ~RecordCallbackProcessor() = default;

  // Process an inbound record callback by passing cached records to it
  // and (if needed) registering the callback.
  virtual void processCallback(const RecordCbSharedPtr& callback) PURE;

  // Remove the callback (usually invoked by the callback timing out downstream).
  virtual void removeCallback(const RecordCbSharedPtr& callback) PURE;
};

using RecordCallbackProcessorSharedPtr = std::shared_ptr<RecordCallbackProcessor>;

/**
 * Manages (raw) Kafka consumers pointing to upstream Kafka clusters.
 * It is expected to have only one instance of this object per mesh-filter type.
 */
class SharedConsumerManager {
public:
  virtual ~SharedConsumerManager() = default;

  // Start the consumer (if there is none) to make sure that records can be received from the topic.
  virtual void registerConsumerIfAbsent(const std::string& topic) PURE;
};

using SharedConsumerManagerPtr = std::unique_ptr<SharedConsumerManager>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
