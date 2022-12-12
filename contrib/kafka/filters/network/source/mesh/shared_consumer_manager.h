#pragma once

#include <map>
#include <vector>

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Manages (raw) Kafka consumers pointing to upstream Kafka clusters.
 * It is expected to have only one instance of this object in the runtime.
 */
class SharedConsumerManager {
public:
  virtual ~SharedConsumerManager() = default;

  // Start the consumer (if there is none) to make sure that records can be received from the topic.
  virtual void registerConsumerIfAbsent(const std::string& topic) PURE;
};

using SharedConsumerManagerSharedPtr = std::shared_ptr<SharedConsumerManager>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
