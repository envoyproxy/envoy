#include "source/extensions/filters/http/cache_v2/range_utils.h"

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
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range) {
  return os << "[" << range.begin() << "," << range.end() << ")";
}

AdjustedByteRange RangeUtils::rangeFromHeaders(Http::ResponseHeaderMap& response_headers) {
  if (Http::Utility::getResponseStatus(response_headers) !=
      static_cast<uint64_t>(Envoy::Http::Code::PartialContent)) {
    // Don't use content-length; we can just request *all the body* from
    // the source and it will tell us when it gets to the end.
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  Http::HeaderMap::GetResult content_range_result =
      response_headers.get(Envoy::Http::Headers::get().ContentRange);
  if (content_range_result.empty()) {
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  absl::string_view content_range = content_range_result[0]->value().getStringView();
  if (!absl::ConsumePrefix(&content_range, "bytes ")) {
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  if (absl::ConsumePrefix(&content_range, "*/")) {
    uint64_t len;
    if (absl::SimpleAtoi(content_range, &len)) {
      return {0, len};
    }
    return {0, std::numeric_limits<uint64_t>::max()};
  }
  std::pair<absl::string_view, absl::string_view> range_of = absl::StrSplit(content_range, '/');
  std::pair<absl::string_view, absl::string_view> range = absl::StrSplit(range_of.first, '-');
  uint64_t begin, end;
  if (!absl::SimpleAtoi(range.first, &begin)) {
    begin = 0;
  }
  if (!absl::SimpleAtoi(range.second, &end)) {
    end = std::numeric_limits<uint64_t>::max();
  } else {
    end++;
  }
  return {begin, end};
}

std::optional<RangeDetails>
RangeUtils::createRangeDetails(const Envoy::Http::RequestHeaderMap& request_headers,
                               uint64_t content_length) {
  if (std::optional<absl::string_view> range_header = RangeUtils::getRangeHeader(request_headers);
      range_header.has_value()) {
    return RangeUtils::createRangeDetails(range_header.value(), content_length);
  }
  return std::nullopt;
}

std::optional<RangeDetails> RangeUtils::createRangeDetails(const absl::string_view range_header,
                                                           const uint64_t content_length) {
  // TODO(cbdm): using a constant limit of 1 range since we don't support
  // multi-part responses nor coalesce multiple overlapping ranges. Could make
  // this into a parameter based on config.
  const int RangeSpecifierLimit = 1;
  std::optional<std::vector<RawByteRange>> request_range_spec =
      RangeUtils::parseRangeHeader(range_header, RangeSpecifierLimit);
  if (!request_range_spec.has_value()) {
    return std::nullopt;
  }

  return RangeUtils::createAdjustedRangeDetails(request_range_spec.value(), content_length);
}

std::optional<absl::string_view>
RangeUtils::getRangeHeader(const Envoy::Http::RequestHeaderMap& headers) {
  const Envoy::Http::HeaderMap::GetResult range_header =
      headers.get(Envoy::Http::Headers::get().Range);
  if (range_header.size() == 1) {
    return range_header[0]->value().getStringView();
  } else {
    return std::nullopt;
  }
}

// TODO(kiehl): Write tests now that this function is stand alone.
RangeDetails
RangeUtils::createAdjustedRangeDetails(const std::vector<RawByteRange>& request_range_spec,
                                       uint64_t content_length) {
  if (request_range_spec.empty()) {
    // No range header, so the request can proceed.
    return {true, {}};
  }

  if (content_length == 0) {
    // There is a range header, but it's unsatisfiable.
    return {false, {}};
  }

  RangeDetails result;
  for (const RawByteRange& spec : request_range_spec) {
    if (spec.isSuffix()) {
      // spec is a suffix-byte-range-spec.
      if (spec.suffixLength() == 0) {
        // This range is unsatisfiable.
        return {false, {}};
      }
      if (spec.suffixLength() >= content_length) {
        // All bytes are being requested, so we may as well send a '200
        // OK' response.
        return {true, {}};
      }
      result.ranges_.emplace_back(content_length - spec.suffixLength(), content_length);
    } else {
      // spec is a byte-range-spec
      if (spec.firstBytePos() >= content_length) {
        // This range is unsatisfiable.
        return {false, {}};
      }
      if (spec.lastBytePos() >= content_length - 1) {
        if (spec.firstBytePos() == 0) {
          // All bytes are being requested, so we may as well send a '200
          // OK' response.
          return {true, {}};
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

std::optional<std::vector<RawByteRange>>
RangeUtils::parseRangeHeader(absl::string_view range_header, uint64_t max_byte_range_specs) {
  if (!absl::ConsumePrefix(&range_header, "bytes=")) {
    return std::nullopt;
  }

  std::vector<absl::string_view> ranges =
      absl::StrSplit(range_header, absl::MaxSplits(',', max_byte_range_specs));
  if (ranges.size() > max_byte_range_specs) {
    return std::nullopt;
  }
  std::vector<RawByteRange> parsed_ranges;
  for (absl::string_view cur_range : ranges) {
    std::optional<uint64_t> first = CacheHeadersUtils::readAndRemoveLeadingDigits(cur_range);

    if (!absl::ConsumePrefix(&cur_range, "-")) {
      return std::nullopt;
    }

    std::optional<uint64_t> last = CacheHeadersUtils::readAndRemoveLeadingDigits(cur_range);

    if (!cur_range.empty()) {
      return std::nullopt;
    }

    if (!first && !last) {
      return std::nullopt;
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
      return std::nullopt;
    }

    parsed_ranges.push_back(RawByteRange(first.value(), last.value()));
  }

  return parsed_ranges;
}

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
