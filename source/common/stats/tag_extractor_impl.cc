#include "source/common/stats/tag_extractor_impl.h"

#include <cstring>
#include <string>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/perf_annotation.h"
#include "source/common/common/regex.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {
namespace {
std::regex parseStdRegex(const std::string& regex) {
  TRY_ASSERT_MAIN_THREAD { return std::regex(regex, std::regex::optimize); }
  END_TRY
  CATCH(const std::regex_error& e,
        { throw EnvoyException(fmt::format("Invalid regex '{}': {}", regex, e.what())); });
}
} // namespace

const std::vector<absl::string_view>& TagExtractionContext::tokens() {
  if (tokens_.empty()) {
    tokens_ = absl::StrSplit(name_, '.');
  }
  return tokens_;
}

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

absl::StatusOr<TagExtractorPtr>
TagExtractorImplBase::createTagExtractor(absl::string_view name, absl::string_view regex,
                                         absl::string_view substr, absl::string_view negative_match,
                                         Regex::Type re_type) {
  if (name.empty()) {
    return absl::InvalidArgumentError("tag_name cannot be empty");
  }

  if (regex.empty()) {
    return absl::InvalidArgumentError(fmt::format(
        "No regex specified for tag specifier and no default regex for name: '{}'", name));
  }
  switch (re_type) {
  case Regex::Type::Re2:
    return std::make_unique<TagExtractorRe2Impl>(name, regex, substr, negative_match);
  case Regex::Type::StdRegex:
    ASSERT(negative_match.empty(), "Not supported");
    return std::make_unique<TagExtractorStdRegexImpl>(name, regex, substr);
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool TagExtractorImplBase::substrMismatch(absl::string_view stat_name) const {
  return !substr_.empty() && stat_name.find(substr_) == absl::string_view::npos;
}

TagExtractorStdRegexImpl::TagExtractorStdRegexImpl(absl::string_view name, absl::string_view regex,
                                                   absl::string_view substr)
    : TagExtractorImplBase(name, regex, substr), regex_(parseStdRegex(std::string(regex))) {}

std::string& TagExtractorImplBase::addTagReturningValueRef(std::vector<Tag>& tags) const {
  tags.emplace_back();
  Tag& tag = tags.back();
  tag.name_ = name_;
  return tag.value_;
}

bool TagExtractorStdRegexImpl::extractTag(TagExtractionContext& context, std::vector<Tag>& tags,
                                          IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);

  absl::string_view stat_name = context.name();
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
    addTagReturningValueRef(tags) = value_subexpr.str();

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
                                         absl::string_view substr, absl::string_view negative_match)
    : TagExtractorImplBase(name, regex, substr), regex_(std::string(regex)),
      negative_match_(std::string(negative_match)) {}

bool TagExtractorRe2Impl::extractTag(TagExtractionContext& context, std::vector<Tag>& tags,
                                     IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);

  absl::string_view stat_name = context.name();
  if (substrMismatch(stat_name)) {
    PERF_RECORD(perf, "re2-skip", name_);
    PERF_TAG_INC(skipped_);
    return false;
  }

  // remove_subexpr is the first submatch. It represents the portion of the string to be removed.
  absl::string_view remove_subexpr, value_subexpr;

  // The regex must match and contain one or more subexpressions (all after the first are ignored).
  if (re2::RE2::PartialMatch(stat_name, regex_, &remove_subexpr, &value_subexpr) &&
      !remove_subexpr.empty()) {

    // value_subexpr is the optional second submatch. It is usually inside the first submatch
    // (remove_subexpr) to allow the expression to strip off extra characters that should be removed
    // from the string but also not necessary in the tag value ("." for example). If there is no
    // second submatch, then the value_subexpr is the same as the remove_subexpr.
    if (value_subexpr.empty()) {
      value_subexpr = remove_subexpr;
    }

    if (negative_match_.empty() || negative_match_ != value_subexpr) {
      addTagReturningValueRef(tags) = std::string(value_subexpr);

      // Determines which characters to remove from stat_name to elide remove_subexpr.
      std::string::size_type start = remove_subexpr.data() - stat_name.data();
      std::string::size_type end = remove_subexpr.data() + remove_subexpr.size() - stat_name.data();
      remove_characters.insert(start, end);

      PERF_RECORD(perf, "re2-match", name_);
      PERF_TAG_INC(matched_);
      return true;
    }
  }
  PERF_RECORD(perf, "re2-miss", name_);
  PERF_TAG_INC(missed_);
  return false;
}

TagExtractorTokensImpl::TagExtractorTokensImpl(absl::string_view name, absl::string_view tokens)
    : TagExtractorImplBase(name, tokens, ""), tokens_(absl::StrSplit(tokens, '.')),
      match_index_(findMatchIndex(tokens_)) {
  if (!tokens_.empty()) {
    const absl::string_view first = tokens_[0];
    if (first != "$" && first != "*" && first != "**") {
      prefix_ = std::string(first);
    }
  }
}

uint32_t TagExtractorTokensImpl::findMatchIndex(const std::vector<std::string>& tokens) {
  for (uint32_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "$") {
      return i;
    }
  }
  ASSERT(false, absl::StrCat("did not find match in ", absl::StrJoin(tokens, ".")));
  return 0;
}

bool TagExtractorTokensImpl::extractTag(TagExtractionContext& context, std::vector<Tag>& tags,
                                        IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);
  const std::vector<absl::string_view>& input_tokens = context.tokens();
  uint32_t match_input_index = input_tokens.size(), start = 0;
  if (!searchTags(input_tokens, 0, 0, 0, start, match_input_index)) {
    PERF_RECORD(perf, "tokens-miss", name_);
    PERF_TAG_INC(missed_);
    return false;
  }
  const absl::string_view tag_value = input_tokens[match_input_index];

  // Given the starting character-index of the match token, we have to
  // choose some character-bounds to remove from the elaborated stat-name
  // in order to construct the tag-extracted name. There are 3 cases.
  // Assume we are matching against "ab.cd.ef".
  //                   char indexes 01234567
  //
  //   1. remove "ab." at the beginning:   remove char-indexes [0,2].
  //   2. remove "cd." in the middle:      remove char-indexes [3,5].
  //   3. remove ".ef" at the end:         remove char-indexes [5,7].
  //
  // Note that if two tag extractors matches on the last two tokens, we'll be
  // left with a trailing dot in the tag-extracted name, e.g. "ab."  However we
  // need this policy of removing the leading dot to work coherently with
  // stat-names that match with a regexes that explicitly calls out the match
  // scope, and in practice this does not occur, as usually the match comes
  // right after a keyword, and we'll never want the keyword as a tag-value
  // itself.
  uint32_t end = start + tag_value.size();
  if (match_input_index < (input_tokens.size() - 1)) {
    ++end; // Remove dot leading to next token, e.g. "ab." or "cd."
  } else if (start > 0) {
    --start; // Remove the dot prior to the lat token, e.g. ".ef"
  }
  addTagReturningValueRef(tags) = std::string(tag_value);
  remove_characters.insert(start, end);

  PERF_RECORD(perf, "tokens-match", name_);
  PERF_TAG_INC(matched_);
  return true;
}

bool TagExtractorTokensImpl::searchTags(const std::vector<absl::string_view>& input_tokens,
                                        uint32_t input_index, uint32_t pattern_index,
                                        uint32_t char_index, uint32_t& start,
                                        uint32_t& match_input_index) const {
  for (; input_index < input_tokens.size() && pattern_index < tokens_.size();
       ++input_index, ++pattern_index) {
    if (pattern_index == match_index_) {
      start = char_index;
      match_input_index = input_index;
    } else {
      const absl::string_view expected_token = tokens_[pattern_index];
      if (expected_token == "**") {
        if (pattern_index == tokens_.size() - 1) {
          pattern_index = tokens_.size();
          input_index = input_tokens.size();
          break;
        }

        // A "**" in the pattern anywhere except the end means that we must find
        // a match for the remainder of the pattern anywhere in the
        // input. Consider pattern "a.**.b.c.$" and input "a.x.b.b.c.d". We
        // don't want to only match the "**" against "x" when it should match
        // against "x.b". Thus we recurse looking for a complete match, starting
        // from each possible suffix until we find a match.
        ++pattern_index;
        for (; input_index < input_tokens.size(); ++input_index) {
          if (searchTags(input_tokens, input_index, pattern_index, char_index, start,
                         match_input_index)) {
            return true;
          }
          char_index += input_tokens[input_index].size() + 1;
        }
        return false;
      }
      const absl::string_view input_token = input_tokens[input_index];
      if (expected_token != "*" && expected_token != input_token) {
        return false;
      }
      char_index += input_token.size() + 1;
    }
  }
  return pattern_index == tokens_.size() && input_index == input_tokens.size();
}

TagExtractorFixedImpl::TagExtractorFixedImpl(absl::string_view name, absl::string_view value)
    : TagExtractorImplBase(name, value), value_(std::string(value)) {}

bool TagExtractorFixedImpl::extractTag(TagExtractionContext&, std::vector<Tag>& tags,
                                       IntervalSet<size_t>&) const {
  addTagReturningValueRef(tags) = value_;
  return true;
}

} // namespace Stats
} // namespace Envoy
