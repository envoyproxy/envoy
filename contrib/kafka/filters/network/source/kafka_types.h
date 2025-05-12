#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Nullable string used by Kafka.
 */
using NullableString = absl::optional<std::string>;

/**
 * Bytes array used by Kafka.
 */
using Bytes = std::vector<unsigned char>;

/**
 * Nullable bytes array used by Kafka.
 */
using NullableBytes = absl::optional<Bytes>;

/**
 * Kafka array of elements of type T.
 */
template <typename T> using NullableArray = absl::optional<std::vector<T>>;

/**
 * Analogous to:
 * https://github.com/apache/kafka/blob/2.8.1/clients/src/main/java/org/apache/kafka/common/Uuid.java#L28
 */
struct Uuid {

  const int64_t msb_;
  const int64_t lsb_;

  Uuid(const int64_t msb, const int64_t lsb) : msb_{msb}, lsb_{lsb} {};

  bool operator==(const Uuid& rhs) const { return msb_ == rhs.msb_ && lsb_ == rhs.lsb_; };
};

/**
 * Kafka topic-partition pair.
 */
using KafkaPartition = std::pair<std::string, int32_t>;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
