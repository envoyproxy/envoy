#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Nullable string used by Kafka.
 */
typedef absl::optional<std::string> NullableString;

/**
 * Bytes array used by Kafka.
 */
typedef std::vector<unsigned char> Bytes;

/**
 * Nullable bytes array used by Kafka.
 */
typedef absl::optional<Bytes> NullableBytes;

/**
 * Kafka array of elements of type T.
 */
template <typename T> using NullableArray = absl::optional<std::vector<T>>;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
