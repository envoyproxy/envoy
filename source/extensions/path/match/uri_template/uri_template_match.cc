#include "source/extensions/path/match/uri_template/uri_template_match.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "source/common/http/path_utility.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

bool UriTemplateMatcher::match(absl::string_view path) const {
  return RE2::FullMatch(Http::PathUtil::removeQueryAndFragment(path), matching_pattern_regex_);
}

absl::string_view UriTemplateMatcher::uriTemplate() const { return path_template_; }

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
