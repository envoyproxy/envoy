#include "source/extensions/pattern_template/pattern_template_matching.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/pattern_template/v3/pattern_template_rewrite.pb.h"

#include "source/common/http/path_utility.h"
#include "source/extensions/pattern_template/pattern_template_matching_internal.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace PatternTemplate {

using PatternTemplateInternal::ParsedUrlPattern;

#ifndef SWIG
// Silence warnings about missing initializers for members of LazyRE2.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

inline re2::StringPiece ToStringPiece(absl::string_view text) { return {text.data(), text.size()}; }

absl::StatusOr<std::string> convertURLPatternSyntaxToRegex(absl::string_view url_pattern) {

  absl::StatusOr<ParsedUrlPattern> status =
      PatternTemplateInternal::parseURLPatternSyntax(url_pattern);
  if (!status.ok()) {
    return status.status();
  }
  struct ParsedUrlPattern pattern = *std::move(status);
  return PatternTemplateInternal::toRegexPattern(pattern);
}

absl::StatusOr<std::vector<RewritePatternSegment>>
parseRewritePatternHelper(absl::string_view pattern) {
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
      if (!PatternTemplateInternal::isValidRewriteLiteral(segments1[0])) {
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

    if (!PatternTemplateInternal::isValidIndent(segments2[0])) {
      return absl::InvalidArgumentError("Invalid variable name");
    }
    result.emplace_back(segments2[0], RewriteStringKind::kVariable);
  }
  return result;
}

absl::StatusOr<envoy::extensions::pattern_template::v3::PatternTemplateRewrite>
parseRewritePattern(absl::string_view pattern, absl::string_view capture_regex) {
  envoy::extensions::pattern_template::v3::PatternTemplateRewrite parsed_pattern;
  RE2 regex = RE2(ToStringPiece(capture_regex));
  if (!regex.ok()) {
    return absl::InternalError(regex.error());
  }

  absl::StatusOr<std::vector<RewritePatternSegment>> status = parseRewritePatternHelper(pattern);
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

absl::Status isValidPathTemplateMatchPattern(const std::string& path_template_match) {
  return convertURLPatternSyntaxToRegex(path_template_match).status();
}

absl::Status isValidPathTemplateRewritePattern(const std::string& path_template_rewrite) {
  return parseRewritePatternHelper(path_template_rewrite).status();
}

absl::Status isValidSharedVariableSet(const std::string& path_template_rewrite,
                                      std::string& capture_regex) {
  return parseRewritePattern(path_template_rewrite, capture_regex).status();
}

absl::Status is_valid_match_pattern(std::string match_pattern) {
  return isValidPathTemplateMatchPattern(match_pattern);
};

absl::Status PatternTemplatePredicate::is_valid_rewrite_pattern(std::string match_pattern,
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

bool PatternTemplatePredicate::match(absl::string_view pattern) const {
  return RE2::FullMatch(ToStringPiece(Http::PathUtil::removeQueryAndFragment(pattern)),
                        matching_pattern_regex_);
}

absl::StatusOr<std::string>
PatternTemplatePredicate::rewritePattern(absl::string_view current_pattern,
                                         absl::string_view matched_path) const {
  absl::StatusOr<std::string> regex_pattern = convertURLPatternSyntaxToRegex(matched_path);
  if (!regex_pattern.ok()) {
    return absl::InvalidArgumentError("Unable to parse url pattern regex");
  }
  std::string regex_pattern_str = *std::move(regex_pattern);

  absl::StatusOr<envoy::extensions::pattern_template::v3::PatternTemplateRewrite> rewrite_pattern =
      parseRewritePattern(url_rewrite_pattern_, regex_pattern_str);

  if (!rewrite_pattern.ok()) {
    return absl::InvalidArgumentError("Unable to parse url rewrite pattern");
  }

  envoy::extensions::pattern_template::v3::PatternTemplateRewrite rewrite_pattern_proto =
      *std::move(rewrite_pattern);

  absl::StatusOr<std::string> new_path =
      rewriteURLTemplatePattern(current_pattern, regex_pattern_str, rewrite_pattern_proto);

  if (!new_path.ok()) {
    return absl::InvalidArgumentError("Unable rewrite url to new URL");
  }

  return *std::move(new_path);
}

absl::StatusOr<std::string> PatternTemplatePredicate::rewriteURLTemplatePattern(
    absl::string_view url, absl::string_view capture_regex,
    const envoy::extensions::pattern_template::v3::PatternTemplateRewrite& rewrite_pattern) const {
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

  for (const envoy::extensions::pattern_template::v3::PatternTemplateRewrite::RewriteSegment&
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

} // namespace PatternTemplate
} // namespace Envoy
