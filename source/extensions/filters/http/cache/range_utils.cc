#include "source/extensions/filters/http/cache/range_utils.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range) {
  return os << "[" << range.begin() << "," << range.end() << ")";
}

absl::optional<RangeDetails>
RangeUtils::createRangeDetails(const Envoy::Http::RequestHeaderMap& request_headers,
                               uint64_t content_length) {
  if (absl::optional<absl::string_view> range_header = RangeUtils::getRangeHeader(request_headers);
      range_header.has_value()) {
    return RangeUtils::createRangeDetails(range_header.value(), content_length);
  }
  return absl::nullopt;
}

absl::optional<RangeDetails> RangeUtils::createRangeDetails(const absl::string_view range_header,
                                                            const uint64_t content_length) {
  // TODO(cbdm): using a constant limit of 1 range since we don't support
  // multi-part responses nor coalesce multiple overlapping ranges. Could make
  // this into a parameter based on config.
  const int RangeSpecifierLimit = 1;
  absl::optional<std::vector<RawByteRange>> request_range_spec =
      RangeUtils::parseRangeHeader(range_header, RangeSpecifierLimit);
  if (!request_range_spec.has_value()) {
    return absl::nullopt;
  }

  return RangeUtils::createAdjustedRangeDetails(request_range_spec.value(), content_length);
}

absl::optional<absl::string_view>
RangeUtils::getRangeHeader(const Envoy::Http::RequestHeaderMap& headers) {
  const Envoy::Http::HeaderMap::GetResult range_header =
      headers.get(Envoy::Http::Headers::get().Range);
  if (range_header.size() == 1) {
    return range_header[0]->value().getStringView();
  } else {
    return absl::nullopt;
  }
}

// TODO(kiehl): Write tests now that this function is stand alone.
RangeDetails
RangeUtils::createAdjustedRangeDetails(const std::vector<RawByteRange>& request_range_spec,
                                       uint64_t content_length) {
  RangeDetails result;
  if (request_range_spec.empty()) {
    // No range header, so the request can proceed.
    result.satisfiable_ = true;
    return result;
  }

  if (content_length == 0) {
    // There is a range header, but it's unsatisfiable.
    result.satisfiable_ = false;
    return result;
  }

  for (const RawByteRange& spec : request_range_spec) {
    if (spec.isSuffix()) {
      // spec is a suffix-byte-range-spec.
      if (spec.suffixLength() == 0) {
        // This range is unsatisfiable, so skip it.
        continue;
      }
      if (spec.suffixLength() >= content_length) {
        // All bytes are being requested, so we may as well send a '200
        // OK' response.
        result.ranges_.clear();
        result.satisfiable_ = true;
        return result;
      }
      result.ranges_.emplace_back(content_length - spec.suffixLength(), content_length);
    } else {
      // spec is a byte-range-spec
      if (spec.firstBytePos() >= content_length) {
        // This range is unsatisfiable, so skip it.
        continue;
      }
      if (spec.lastBytePos() >= content_length - 1) {
        if (spec.firstBytePos() == 0) {
          // All bytes are being requested, so we may as well send a '200
          // OK' response.

          result.ranges_.clear();
          result.satisfiable_ = true;
          return result;
        }
        result.ranges_.emplace_back(spec.firstBytePos(), content_length);
      } else {
        result.ranges_.emplace_back(spec.firstBytePos(), spec.lastBytePos() + 1);
      }
    }
  }

  result.satisfiable_ = !result.ranges_.empty();

  return result;
}

absl::optional<std::vector<RawByteRange>>
RangeUtils::parseRangeHeader(absl::string_view range_header, uint64_t max_byte_range_specs) {
  if (!absl::ConsumePrefix(&range_header, "bytes=")) {
    return absl::nullopt;
  }

  std::vector<absl::string_view> ranges =
      absl::StrSplit(range_header, absl::MaxSplits(',', max_byte_range_specs));
  if (ranges.size() > max_byte_range_specs) {
    return absl::nullopt;
  }
  std::vector<RawByteRange> parsed_ranges;
  for (absl::string_view cur_range : ranges) {
    absl::optional<uint64_t> first = CacheHeadersUtils::readAndRemoveLeadingDigits(cur_range);

    if (!absl::ConsumePrefix(&cur_range, "-")) {
      return absl::nullopt;
    }

    absl::optional<uint64_t> last = CacheHeadersUtils::readAndRemoveLeadingDigits(cur_range);

    if (!cur_range.empty()) {
      return absl::nullopt;
    }

    if (!first && !last) {
      return absl::nullopt;
    }

    // Handle suffix range (e.g., -123).
    if (!first) {
      first = std::numeric_limits<uint64_t>::max();
    }

    // Handle optional range-end (e.g., 123-).
    if (!last) {
      last = std::numeric_limits<uint64_t>::max();
    }

    if (first != std::numeric_limits<uint64_t>::max() && first > last) {
      return absl::nullopt;
    }

    parsed_ranges.push_back(RawByteRange(first.value(), last.value()));
  }

  return parsed_ranges;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
