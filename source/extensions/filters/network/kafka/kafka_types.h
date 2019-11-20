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

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
