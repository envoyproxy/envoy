#pragma once

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Simple structure representing the record received from upstream Kafka cluster.
 */
struct InboundRecord {

  const std::string topic_;
  const int32_t partition_;
  const int64_t offset_;

  // TODO (adam.kotwasinski) Get data in here in the next commits.

  InboundRecord(std::string topic, int32_t partition, int64_t offset)
      : topic_{topic}, partition_{partition}, offset_{offset} {};

  // Used in logging.
  std::string toString() const {
    return absl::StrCat("[", topic_, "-", partition_, "/", offset_, "]");
  }
};

using InboundRecordSharedPtr = std::shared_ptr<InboundRecord>;

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
