#pragma once

#include <memory>
#include <string>

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Represents a sequence of characters or null. For non-null strings, first the length N is given as
 * an INT16. Then N bytes follow which are the UTF-8 encoding of the character sequence. A null
 * value is encoded with length of -1 and there are no following bytes.
 */
typedef absl::optional<std::string> NullableString;

/**
 * Represents a raw sequence of bytes.
 * First the length N is given as an INT32. Then N bytes follow.
 */
typedef std::vector<unsigned char> Bytes;

/**
 * Represents a raw sequence of bytes or null. For non-null values, first the length N is given as
 * an INT32. Then N bytes follow. A null value is encoded with length of -1 and there are no
 * following bytes.
 */
typedef absl::optional<Bytes> NullableBytes;

/**
 * Represents a sequence of objects of a given type T.
 * Type T can be either a primitive type (e.g. STRING) or a structure.
 * First, the length N is given as an INT32.
 * Then N instances of type T follow.
 * A null array is represented with a length of -1.
 */
template <typename T> using NullableArray = absl::optional<std::vector<T>>;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
