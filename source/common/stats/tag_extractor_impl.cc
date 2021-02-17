#include "common/stats/tag_extractor_impl.h"

#include <cstring>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/perf_annotation.h"
#include "common/common/regex.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

namespace {

bool regexStartsWithDot(absl::string_view regex) {
  return absl::StartsWith(regex, "\\.") || absl::StartsWith(regex, "(?=\\.)");
}

} // namespace

TagExtractorImplBase::TagExtractorImplBase(absl::string_view name, absl::string_view regex,
                                           absl::string_view substr)
    : name_(name), prefix_(std::string(extractRegexPrefix(regex))), substr_(substr) {
  PERF_TAG_INIT;
}

std::string TagExtractorImplBase::extractRegexPrefix(absl::string_view regex) {
  std::string prefix;
  if (absl::StartsWith(regex, "^")) {
    for (absl::string_view::size_type i = 1; i < regex.size(); ++i) {
      if (!absl::ascii_isalnum(regex[i]) && (regex[i] != '_')) {
        if (i > 1) {
          const bool last_char = i == regex.size() - 1;
          if ((!last_char && regexStartsWithDot(regex.substr(i))) ||
              (last_char && (regex[i] == '$'))) {
            prefix.append(regex.data() + 1, i - 1);
          }
        }
        break;
      }
    }
  }
  return prefix;
}

TagExtractorPtr TagExtractorImplBase::createTagExtractor(absl::string_view name,
                                                         absl::string_view regex,
                                                         absl::string_view substr,
                                                         Regex::Type re_type) {
  if (name.empty()) {
    throw EnvoyException("tag_name cannot be empty");
  }

  if (regex.empty()) {
    throw EnvoyException(fmt::format(
        "No regex specified for tag specifier and no default regex for name: '{}'", name));
  }
  switch (re_type) {
  case Regex::Type::Re2:
    return std::make_unique<TagExtractorRe2Impl>(name, regex, substr);
  case Regex::Type::StdRegex:
    return std::make_unique<TagExtractorStdRegexImpl>(name, regex, substr);
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

bool TagExtractorImplBase::substrMismatch(absl::string_view stat_name) const {
  return !substr_.empty() && stat_name.find(substr_) == absl::string_view::npos;
}

TagExtractorStdRegexImpl::TagExtractorStdRegexImpl(absl::string_view name, absl::string_view regex,
                                                   absl::string_view substr)
    : TagExtractorImplBase(name, regex, substr),
      regex_(Regex::Utility::parseStdRegex(std::string(regex))) {}

std::string& TagExtractorImplBase::addTag(std::vector<Tag>& tags) const {
  tags.emplace_back();
  Tag& tag = tags.back();
  tag.name_ = name_;
  return tag.value_;
}

bool TagExtractorStdRegexImpl::extractTag(absl::string_view stat_name, std::vector<Tag>& tags,
                                          IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);

  if (substrMismatch(stat_name)) {
    PERF_RECORD(perf, "re-skip", name_);
    PERF_TAG_INC(skipped_);
    return false;
  }

  std::match_results<absl::string_view::iterator> match;
  // The regex must match and contain one or more subexpressions (all after the first are ignored).
  if (std::regex_search<absl::string_view::iterator>(stat_name.begin(), stat_name.end(), match,
                                                     regex_) &&
      match.size() > 1) {
    // remove_subexpr is the first submatch. It represents the portion of the string to be removed.
    const auto& remove_subexpr = match[1];

    // value_subexpr is the optional second submatch. It is usually inside the first submatch
    // (remove_subexpr) to allow the expression to strip off extra characters that should be removed
    // from the string but also not necessary in the tag value ("." for example). If there is no
    // second submatch, then the value_subexpr is the same as the remove_subexpr.
    const auto& value_subexpr = match.size() > 2 ? match[2] : remove_subexpr;
    addTag(tags) = value_subexpr.str();

    // Determines which characters to remove from stat_name to elide remove_subexpr.
    std::string::size_type start = remove_subexpr.first - stat_name.begin();
    std::string::size_type end = remove_subexpr.second - stat_name.begin();
    remove_characters.insert(start, end);
    PERF_RECORD(perf, "re-match", name_);
    PERF_TAG_INC(matched_);
    return true;
  }
  PERF_RECORD(perf, "re-miss", name_);
  PERF_TAG_INC(missed_);
  return false;
}

TagExtractorRe2Impl::TagExtractorRe2Impl(absl::string_view name, absl::string_view regex,
                                         absl::string_view substr)
    : TagExtractorImplBase(name, regex, substr), regex_(regex) {}

bool TagExtractorRe2Impl::extractTag(absl::string_view stat_name, std::vector<Tag>& tags,
                                     IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);

  if (substrMismatch(stat_name)) {
    PERF_RECORD(perf, "re2-skip", name_);
    PERF_TAG_INC(skipped_);
    return false;
  }

  // remove_subexpr is the first submatch. It represents the portion of the string to be removed.
  re2::StringPiece remove_subexpr, value_subexpr;

  // The regex must match and contain one or more subexpressions (all after the first are ignored).
  if (re2::RE2::PartialMatch(re2::StringPiece(stat_name.data(), stat_name.size()), regex_,
                             &remove_subexpr, &value_subexpr) &&
      !remove_subexpr.empty()) {

    // value_subexpr is the optional second submatch. It is usually inside the first submatch
    // (remove_subexpr) to allow the expression to strip off extra characters that should be removed
    // from the string but also not necessary in the tag value ("." for example). If there is no
    // second submatch, then the value_subexpr is the same as the remove_subexpr.
    if (value_subexpr.empty()) {
      value_subexpr = remove_subexpr;
    }
    addTag(tags) = std::string(value_subexpr);

    // Determines which characters to remove from stat_name to elide remove_subexpr.
    std::string::size_type start = remove_subexpr.data() - stat_name.data();
    std::string::size_type end = remove_subexpr.data() + remove_subexpr.size() - stat_name.data();
    remove_characters.insert(start, end);

    PERF_RECORD(perf, "re2-match", name_);
    PERF_TAG_INC(matched_);
    return true;
  }
  PERF_RECORD(perf, "re2-miss", name_);
  PERF_TAG_INC(missed_);
  return false;
}

TagExtractorTokensImpl::TagExtractorTokensImpl(absl::string_view name, absl::string_view tokens,
                                               absl::string_view substr)
    : TagExtractorImplBase(name, tokens, substr),
      tokens_(absl::StrSplit(tokens, '.')), match_index_(findMatchIndex(tokens_)) {
  prefix_ = tokens_[0];
}

uint32_t TagExtractorTokensImpl::findMatchIndex(
    const std::vector<std::string>& tokens) {
  for (uint32_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "$") {
      return i;
    }
  }
  return 0;
}

bool TagExtractorTokensImpl::extractTag(absl::string_view stat_name, std::vector<Tag>& tags,
                                        IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);

  if (substrMismatch(stat_name)) {
    PERF_RECORD(perf, "tokens-skip", name_);
    PERF_TAG_INC(skipped_);
    return false;
  }

  std::vector<absl::string_view> tokens = absl::StrSplit(stat_name, '.');
  uint32_t char_index = 0, start = 0, end = 0, input_index = 0, pattern_index = 0;
  absl::string_view tag_value;
  for (; input_index < tokens.size() && pattern_index < tokens_.size();
       ++input_index, ++pattern_index) {
    if (pattern_index == match_index_) {
      tag_value = tokens[input_index];
      start = char_index;
      end = start + tag_value.size();
      if (input_index < (tokens.size() - 1)) {
        ++end; // Remove dot leading to next token
      } else if (start > 0) {
        --start;
      }
    } else {
      absl::string_view expected = tokens_[pattern_index];
      if (expected == "**") {
        if (pattern_index == tokens_.size() - 1) {
          pattern_index = tokens_.size();
          input_index = tokens.size();
          break;
        }

        // A "**" in the pattern anywhere except the end means that
        // we must search for the next expected token.
        ++pattern_index;
        expected = tokens_[pattern_index];
        for (; input_index < tokens.size(); ++input_index) {
          if (tokens[input_index] == expected) {
            break;
          }
          char_index += tokens[input_index].size() + 1;
        }
      }
      if ((expected != "*" && expected != "**") && (expected != tokens[input_index])) {
        PERF_RECORD(perf, "tokens-miss", name_);
        PERF_TAG_INC(missed_);
        return false;
      }
      char_index += tokens[input_index].size() + 1;
    }
  }
  if (pattern_index < tokens_.size() || input_index < tokens.size()) {
    PERF_RECORD(perf, "tokens-miss", name_);
    PERF_TAG_INC(missed_);
    return false;
  }

  remove_characters.insert(start, end);
  addTag(tags) = tag_value;
  PERF_RECORD(perf, "tokens-match", name_);
  PERF_TAG_INC(matched_);
  return true;
}

} // namespace Stats
} // namespace Envoy
