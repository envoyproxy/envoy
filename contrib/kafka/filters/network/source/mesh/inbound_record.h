#pragma once

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
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

  InboundRecord(std::string topic, int32_t partition, int64_t offset, NullableBytes key,
                NullableBytes value)
      : topic_{topic}, partition_{partition}, offset_{offset}, key_{key}, value_{value} {};

  absl::string_view key() const {
    if (key_) {
      return {reinterpret_cast<const char*>(key_->data()), key_->size()};
    } else {
      return {};
    }
  }

  absl::string_view value() const {
    if (value_) {
      return {reinterpret_cast<const char*>(value_->data()), value_->size()};
    } else {
      return {};
    }
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
