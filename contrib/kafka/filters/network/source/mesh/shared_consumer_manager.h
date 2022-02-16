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

  virtual void getRecordsOrRegisterCallback(const RecordCbSharedPtr& callback) PURE;

  virtual void removeCallback(const RecordCbSharedPtr& callback) PURE;
};

using SharedConsumerManagerSharedPtr = std::shared_ptr<SharedConsumerManager>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
