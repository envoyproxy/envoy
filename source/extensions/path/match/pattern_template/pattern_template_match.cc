#include "source/extensions/path/match/pattern_template/pattern_template_match.h"

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
namespace PatternTemplate {
namespace Match {

bool PatternTemplateMatcher::match(absl::string_view path) const {
  RE2 matching_pattern_regex = RE2(convertURLPatternSyntaxToRegex(path_template_).value());
  return RE2::FullMatch(
      PatternTemplateInternal::toStringPiece(Http::PathUtil::removeQueryAndFragment(path)),
      matching_pattern_regex);
}

absl::string_view PatternTemplateMatcher::pattern() const { return path_template_; }

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
