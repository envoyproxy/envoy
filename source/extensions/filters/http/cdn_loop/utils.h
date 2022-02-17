#pragma once

#include "source/common/common/statusor.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

// Count the number of times cdn_id appears as a cdn-id element in header.
//
// According to RFC 8586, a cdn-id is either a uri-host[:port] or a pseudonym.
// In either case, cdn_id must be at least one character long.
//
// If the header is unparseable or if cdn_id is the empty string, this function
// will return an InvalidArgument status.
StatusOr<int> countCdnLoopOccurrences(absl::string_view header, absl::string_view cdn_id);

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
