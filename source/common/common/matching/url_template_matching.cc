#include "source/common/common/matching/url_template_matching.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/matching/url_template_matching_internal.h"
#include "re2/re2.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/str_split.h"

namespace matching {

using ::matching::url_template_matching_internal::ParsedUrlPattern;

#ifndef SWIG
// Silence warnings about missing initializers for members of LazyRE2.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

inline re2::StringPiece ToStringPiece(absl::string_view text) { return {text.data(), text.size()}; }

bool IsPatternMatch(absl::string_view pattern, absl::string_view capture_regex) {
  RE2 regex = RE2(ToStringPiece(capture_regex));
  return RE2::FullMatch(ToStringPiece(pattern), regex);
}

absl::StatusOr<std::string> ConvertURLPatternSyntaxToRegex(absl::string_view url_pattern) {

  absl::StatusOr<ParsedUrlPattern> status =
      url_template_matching_internal::ParseURLPatternSyntax(url_pattern);
  if (!status.ok()) {
    return status.status();
  }
  struct ParsedUrlPattern pattern = *std::move(status);
  return url_template_matching_internal::ToRegexPattern(pattern);
}

absl::StatusOr<std::vector<RewritePatternSegment>>
ParseRewritePatternHelper(absl::string_view pattern) {
  std::vector<RewritePatternSegment> result;

  // Don't allow contiguous '/' patterns.
  static const LazyRE2 invalid_regex = {"^.*//.*$"};
  if (RE2::FullMatch(ToStringPiece(pattern), *invalid_regex)) {
    return absl::InvalidArgumentError("Invalid rewrite literal pattern");
  }

  // The pattern should start with a '/' and thus the first segment should
  // always be a literal.
  if (pattern.empty() || pattern[0] != '/') {
    return absl::InvalidArgumentError("Invalid rewrite variable placement");
  }
  while (!pattern.empty()) {
    std::vector<absl::string_view> segments1 = absl::StrSplit(pattern, absl::MaxSplits('{', 1));
    if (!segments1[0].empty()) {
      if (!url_template_matching_internal::IsValidRewriteLiteral(segments1[0])) {
        return absl::InvalidArgumentError("Invalid rewrite literal pattern");
      }
      result.emplace_back(segments1[0], RewriteStringKind::kLiteral);
    }

    if (segments1.size() < 2) {
      // No more variable replacement, done.
      break;
    }

    std::vector<absl::string_view> segments2 =
        absl::StrSplit(segments1[1], absl::MaxSplits('}', 1));
    if (segments2.size() < 2) {
      return absl::InvalidArgumentError("Unmatched variable bracket");
    }
    pattern = segments2[1];

    if (!url_template_matching_internal::IsValidIdent(segments2[0])) {
      return absl::InvalidArgumentError("Invalid variable name");
    }
    result.emplace_back(segments2[0], RewriteStringKind::kVariable);
  }
  return result;
}

absl::StatusOr<envoy::config::route::v3::RouteUrlRewritePattern>
ParseRewritePattern(absl::string_view pattern, absl::string_view capture_regex) {
  envoy::config::route::v3::RouteUrlRewritePattern parsed_pattern;
  RE2 regex = RE2(ToStringPiece(capture_regex));
  if (!regex.ok()) {
    return absl::InternalError(regex.error());
  }

  absl::StatusOr<std::vector<RewritePatternSegment>> status = ParseRewritePatternHelper(pattern);
  if (!status.ok()) {
    return status.status();
  }
  std::vector<RewritePatternSegment> processed_pattern = *std::move(status);

  const std::map<std::string, int>& capture_index_map = regex.NamedCapturingGroups();

  for (const auto& [str, kind] : processed_pattern) {
    switch (kind) {
    case RewriteStringKind::kLiteral:
      parsed_pattern.add_segments()->set_literal(std::string(str));
      break;
    case RewriteStringKind::kVariable:
      auto it = capture_index_map.find(std::string(str));
      if (it == capture_index_map.end()) {
        return absl::InvalidArgumentError("Nonexisting variable name");
      }
      parsed_pattern.add_segments()->set_var_index(it->second);
      break;
    }
  }

  return parsed_pattern;
}

absl::StatusOr<std::string>
RewriteURLTemplatePattern(absl::string_view url, absl::string_view capture_regex,
                          const envoy::config::route::v3::RouteUrlRewritePattern& rewrite_pattern) {
  RE2 regex = RE2(ToStringPiece(capture_regex));
  if (!regex.ok()) {
    return absl::InternalError(regex.error());
  }
  // First capture is the whole matched regex pattern.
  int capture_num = regex.NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> captures(capture_num);
  if (!regex.Match(ToStringPiece(url), /*startpos=*/0, /*endpos=*/url.size(), RE2::ANCHOR_BOTH,
                   captures.data(), captures.size())) {
    return absl::InvalidArgumentError("Pattern not match");
  }

  std::string rewritten_url;

  for (const envoy::config::route::v3::RouteUrlRewritePattern::RewriteSegment& segment :
       rewrite_pattern.segments()) {
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

bool IsValidPathTemplateMatchPattern(const std::string& path_template_match) {
  return ConvertURLPatternSyntaxToRegex(path_template_match).ok();
}

bool IsValidPathTemplateRewritePattern(const std::string& path_template_rewrite) {
  return ParseRewritePatternHelper(path_template_rewrite).ok();
}

bool IsValidSharedVariableSet(const std::string& path_template_rewrite,
                              absl::string_view capture_regex) {
  return ParseRewritePattern(path_template_rewrite, capture_regex).ok();
}

} // namespace matching