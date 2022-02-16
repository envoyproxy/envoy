#pragma once

#include <memory>
#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Simple structure representing the record received from upstream Kafka cluster.
 */
struct InboundRecord {

  std::string topic_;
  int32_t partition_;
  int64_t offset_;

  // XXX (adam.kotwasinski) Get data in here.

  std::string topic_name() { return topic_; }

  int32_t partition() { return partition_; }

  int64_t offset() { return offset_; }

  InboundRecord(std::string topic, int32_t partition, int64_t offset)
      : topic_{topic}, partition_{partition}, offset_{offset} {};
};

using InboundRecordSharedPtr = std::shared_ptr<InboundRecord>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
