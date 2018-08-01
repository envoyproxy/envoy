#include "common/stats/tag_extractor_impl.h"

#include <string.h>

#include <string>

#include "envoy/common/exception.h"

#include "common/common/perf_annotation.h"
#include "common/common/utility.h"

//#include "common/stats/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Stats {

namespace {

bool regexStartsWithDot(absl::string_view regex) {
  return absl::StartsWith(regex, "\\.") || absl::StartsWith(regex, "(?=\\.)");
}

} // namespace

TagExtractorImpl::TagExtractorImpl(const std::string& name, const std::string& regex,
                                   const std::string& substr)
    : name_(name), prefix_(std::string(extractRegexPrefix(regex))), substr_(substr),
      regex_(RegexUtil::parseRegex(regex)) {}

std::string TagExtractorImpl::extractRegexPrefix(absl::string_view regex) {
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

TagExtractorPtr TagExtractorImpl::createTagExtractor(const std::string& name,
                                                     const std::string& regex,
                                                     const std::string& substr) {

  if (name.empty()) {
    throw EnvoyException("tag_name cannot be empty");
  }

  if (regex.empty()) {
    throw EnvoyException(fmt::format(
        "No regex specified for tag specifier and no default regex for name: '{}'", name));
  }
  return TagExtractorPtr{new TagExtractorImpl(name, regex, substr)};
}

bool TagExtractorImpl::substrMismatch(const std::string& stat_name) const {
  return !substr_.empty() && stat_name.find(substr_) == std::string::npos;
}

bool TagExtractorImpl::extractTag(const std::string& stat_name, std::vector<Tag>& tags,
                                  IntervalSet<size_t>& remove_characters) const {
  PERF_OPERATION(perf);

  if (substrMismatch(stat_name)) {
    PERF_RECORD(perf, "re-skip-substr", name_);
    return false;
  }

  std::smatch match;
  // The regex must match and contain one or more subexpressions (all after the first are ignored).
  if (std::regex_search(stat_name, match, regex_) && match.size() > 1) {
    // remove_subexpr is the first submatch. It represents the portion of the string to be removed.
    const auto& remove_subexpr = match[1];

    // value_subexpr is the optional second submatch. It is usually inside the first submatch
    // (remove_subexpr) to allow the expression to strip off extra characters that should be removed
    // from the string but also not necessary in the tag value ("." for example). If there is no
    // second submatch, then the value_subexpr is the same as the remove_subexpr.
    const auto& value_subexpr = match.size() > 2 ? match[2] : remove_subexpr;

    tags.emplace_back();
    Tag& tag = tags.back();
    tag.name_ = name_;
    tag.value_ = value_subexpr.str();

    // Determines which characters to remove from stat_name to elide remove_subexpr.
    std::string::size_type start = remove_subexpr.first - stat_name.begin();
    std::string::size_type end = remove_subexpr.second - stat_name.begin();
    remove_characters.insert(start, end);
    PERF_RECORD(perf, "re-match", name_);
    return true;
  }
  PERF_RECORD(perf, "re-miss", name_);
  return false;
}

} // namespace Stats
} // namespace Envoy
