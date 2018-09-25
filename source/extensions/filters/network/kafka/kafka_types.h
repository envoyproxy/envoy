#pragma once

#include "absl/types/optional.h"

#include <memory>
#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uint32_t UINT32;
typedef bool BOOLEAN;

typedef std::string STRING;
typedef absl::optional<STRING> NULLABLE_STRING;

typedef std::vector<unsigned char> BYTES;
typedef absl::optional<BYTES> NULLABLE_BYTES;

template<typename T>
using NULLABLE_ARRAY = absl::optional<std::vector<T>>;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
