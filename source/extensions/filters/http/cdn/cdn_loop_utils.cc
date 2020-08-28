#include "extensions/filters/http/cdn/cdn_loop_utils.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "common/common/statusor.h"
#include "extensions/filters/http/cdn/cdn_loop_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cdn {

StatusOr<int> countCdnLoopOccurrences(absl::string_view header, absl::string_view cdn_id) {
  if (cdn_id.empty()) {
    return absl::InvalidArgumentError("cdn_id cannot be empty");
  }

  if (absl::StatusOr<CdnLoopParser::ParsedCdnInfoList> parsed =
          CdnLoopParser::ParseCdnInfoList(header);
      parsed) {
    return std::count(parsed->cdn_ids().begin(), parsed->cdn_ids().end(), cdn_id);
  } else {
    return parsed.status();
  }
}

} // namespace Cdn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
