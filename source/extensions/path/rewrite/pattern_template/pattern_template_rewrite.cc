#include "source/extensions/path/rewrite/pattern_template/pattern_template_rewrite.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "source/common/http/path_utility.h"
#include "source/extensions/path/pattern_template_lib/pattern_template_internal.h"
#include "source/extensions/path/pattern_template_lib/proto/pattern_template_rewrite_segments.pb.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

#ifndef SWIG
// Silence warnings about missing initializers for members of LazyRE2.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

absl::Status PatternTemplateRewritePredicate::isValidRewritePattern(std::string match_pattern,
                                                                    std::string rewrite_pattern) {

  if (!isValidPathTemplateRewritePattern(rewrite_pattern).ok()) {
    return absl::InvalidArgumentError(
        fmt::format("path_template_rewrite {} is invalid", match_pattern));
  }

  absl::StatusOr<std::string> converted_pattern = convertURLPatternSyntaxToRegex(match_pattern);
  if (!converted_pattern.ok()) {
    return absl::InvalidArgumentError(fmt::format("path_template {} is invalid", match_pattern));
  }

  std::string path_template_match_regex = *std::move(converted_pattern);
  if (path_template_match_regex.empty() ||
      !isValidSharedVariableSet(rewrite_pattern, path_template_match_regex).ok()) {
    return absl::InvalidArgumentError(
        fmt::format("mismatch between path_template {} and path_template_rewrite {}", match_pattern,
                    rewrite_pattern));
  }

  return absl::OkStatus();
};

absl::StatusOr<std::string>
PatternTemplateRewritePredicate::rewritePattern(absl::string_view current_pattern,
                                                absl::string_view matched_path) const {
  absl::StatusOr<std::string> regex_pattern = convertURLPatternSyntaxToRegex(matched_path);
  if (!regex_pattern.ok()) {
    return absl::InvalidArgumentError("Unable to parse url pattern regex");
  }
  std::string regex_pattern_str = *std::move(regex_pattern);

  absl::StatusOr<envoy::extensions::pattern_template::PatternTemplateRewriteSegments>
      rewrite_pattern = parseRewritePattern(url_rewrite_pattern_, regex_pattern_str);

  if (!rewrite_pattern.ok()) {
    return absl::InvalidArgumentError("Unable to parse url rewrite pattern");
  }

  envoy::extensions::pattern_template::PatternTemplateRewriteSegments rewrite_pattern_proto =
      *std::move(rewrite_pattern);

  absl::StatusOr<std::string> new_path =
      rewriteURLTemplatePattern(current_pattern, regex_pattern_str, rewrite_pattern_proto);

  if (!new_path.ok()) {
    return absl::InvalidArgumentError("Unable rewrite url to new URL");
  }

  return *std::move(new_path);
}

absl::StatusOr<std::string> PatternTemplateRewritePredicate::rewriteURLTemplatePattern(
    absl::string_view url, absl::string_view capture_regex,
    const envoy::extensions::pattern_template::PatternTemplateRewriteSegments& rewrite_pattern)
    const {
  RE2 regex = RE2(PatternTemplateInternal::toStringPiece(capture_regex));
  if (!regex.ok()) {
    return absl::InternalError(regex.error());
  }

  // First capture is the whole matched regex pattern.
  int capture_num = regex.NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> captures(capture_num);
  if (!regex.Match(PatternTemplateInternal::toStringPiece(url), /*startpos=*/0,
                   /*endpos=*/url.size(), RE2::ANCHOR_BOTH, captures.data(), captures.size())) {
    return absl::InvalidArgumentError("Pattern not match");
  }

  std::string rewritten_url;

  for (const envoy::extensions::pattern_template::PatternTemplateRewriteSegments::RewriteSegment&
           segment : rewrite_pattern.segments()) {
    if (segment.has_literal()) {
      absl::StrAppend(&rewritten_url, segment.literal());
    } else if (segment.has_var_index()) {
      if (segment.var_index() < 1 || segment.var_index() >= capture_num) {
        return absl::InvalidArgumentError("Invalid variable index");
      }
      absl::StrAppend(&rewritten_url, absl::string_view(captures[segment.var_index()].as_string()));
    }
  }

  return rewritten_url;
}

} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
