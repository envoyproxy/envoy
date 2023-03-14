#pragma once

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "contrib/kafka/filters/network/source/kafka_types.h"

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

  const NullableBytes key_;
  const NullableBytes value_;

  InboundRecord(const std::string& topic, const int32_t partition, const int64_t offset,
                const NullableBytes& key, const NullableBytes& value)
      : topic_{topic}, partition_{partition}, offset_{offset}, key_{key}, value_{value} {};

  // Estimates how many bytes this record would take.
  uint32_t dataLengthEstimate() const {
    uint32_t result = 15; // Max key length, value length, header count.
    result += key_ ? key_->size() : 0;
    result += value_ ? value_->size() : 0;
    return result;
  }

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
