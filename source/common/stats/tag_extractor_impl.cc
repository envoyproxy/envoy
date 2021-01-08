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
  PERF_TAG_COUNTERS_INIT(counters_);
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
    PERF_TAG_SKIPPED_INC(counters_);
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
    PERF_TAG_MATCHED_INC(counters_);
    return true;
  }
  PERF_RECORD(perf, "re-miss", name_);
  PERF_TAG_MISSED_INC(counters_);
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
    PERF_TAG_SKIPPED_INC(counters_);
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
    PERF_TAG_MATCHED_INC(counters_);
    return true;
  }
  PERF_RECORD(perf, "re2-miss", name_);
  PERF_TAG_MISSED_INC(counters_);
  return false;
}

} // namespace Stats
} // namespace Envoy
