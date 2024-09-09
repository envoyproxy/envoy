#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/key.pb.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Byte range from an HTTP request.
class RawByteRange {
public:
  // - If first==UINT64_MAX, construct a RawByteRange requesting the final last
  // body bytes.
  // - Otherwise, construct a RawByteRange requesting the [first,last] body
  // bytes. Prereq: first == UINT64_MAX || first <= last Invariant: isSuffix()
  // || firstBytePos() <= lastBytePos Examples: RawByteRange(0,4) requests the
  // first 5 bytes.
  //           RawByteRange(UINT64_MAX,4) requests the last 4 bytes.
  RawByteRange(uint64_t first, uint64_t last) : first_byte_pos_(first), last_byte_pos_(last) {
    ASSERT(isSuffix() || first <= last, "Illegal byte range.");
  }
  bool isSuffix() const { return first_byte_pos_ == UINT64_MAX; }
  uint64_t firstBytePos() const {
    ASSERT(!isSuffix());
    return first_byte_pos_;
  }
  uint64_t lastBytePos() const {
    ASSERT(!isSuffix());
    return last_byte_pos_;
  }
  uint64_t suffixLength() const {
    ASSERT(isSuffix());
    return last_byte_pos_;
  }

private:
  const uint64_t first_byte_pos_;
  const uint64_t last_byte_pos_;
};

// Byte range from an HTTP request, adjusted for a known response body size, and
// converted from an HTTP-style closed interval to a C++ style half-open
// interval.
class AdjustedByteRange {
public:
  // Construct an AdjustedByteRange representing the [first,last) bytes in the
  // response body. Prereq: first <= last Invariant: begin() <= end()
  // Example: AdjustedByteRange(0,4) represents the first 4 bytes.
  AdjustedByteRange(uint64_t first, uint64_t last) : first_(first), last_(last) {
    ASSERT(first < last, "Illegal byte range.");
  }
  uint64_t begin() const { return first_; }

  // Unlike RawByteRange, end() is one past the index of the last offset.
  //
  // If end() == std::numeric_limits<uint64_t>::max(), the cache doesn't yet
  // know the response body's length.
  uint64_t end() const { return last_; }
  uint64_t length() const { return last_ - first_; }
  void trimFront(uint64_t n) {
    ASSERT(n <= length(), "Attempt to trim too much from range.");
    first_ += n;
  }

private:
  uint64_t first_;
  uint64_t last_;
};

inline bool operator==(const AdjustedByteRange& lhs, const AdjustedByteRange& rhs) {
  return lhs.begin() == rhs.begin() && lhs.end() == rhs.end();
}

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range);

// Contains details about whether the ranges requested can be satisfied and, if
// so, what those ranges are after being adjusted to fit the content.
struct RangeDetails {
  // Indicates whether the requested ranges can be satisfied by the content
  // stored in the cache. If not, we need to go to the backend to fill the
  // cache.
  bool satisfiable_ = false;
  // The ranges that will be served by the cache, if satisfiable_ = true.
  std::vector<AdjustedByteRange> ranges_;
};

namespace RangeUtils {
// Create a RangeDetails object from request headers and provided content
// length to assess whether the range request can be satisfied. nullopt
// indicates that this request should not be treated as a range request
// (either it is invalid and ignored, or not a range request at all).
absl::optional<RangeDetails>
createRangeDetails(const Envoy::Http::RequestHeaderMap& request_headers, uint64_t content_length);
absl::optional<RangeDetails> createRangeDetails(const absl::string_view range_header,
                                                const uint64_t content_length);

// Simple utility to extract the range header from the request header map.
absl::optional<absl::string_view> getRangeHeader(const Envoy::Http::RequestHeaderMap& headers);

// Create RangeDetails indicating if the range request is satisfiable, and, if
// so, create adjusted byte ranges to fit the provided content_length.
RangeDetails createAdjustedRangeDetails(const std::vector<RawByteRange>& request_range_spec,
                                        uint64_t content_length);

// Parses the ranges from the request headers into a vector<RawByteRange>.
// max_byte_range_specs defines how many byte ranges can be parsed from the
// header value. If there is no range header, multiple range headers, the
// header value is malformed, or there are more ranges than
// max_byte_range_specs, returns nullopt.
absl::optional<std::vector<RawByteRange>> parseRangeHeader(absl::string_view range_header,
                                                           uint64_t max_byte_range_specs);
} // namespace RangeUtils
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
