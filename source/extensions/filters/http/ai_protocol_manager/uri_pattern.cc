#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include <algorithm>
#include <utility>

#include "source/common/http/path_utility.h"
#include "source/extensions/path/uri_template_lib/uri_template.h"

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

bool isPathToken(absl::string_view value) {
  return !value.empty() && absl::ascii_isalnum(value.front()) &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return absl::ascii_isalnum(c) || c == '-' || c == '.' || c == '_' || c == '@';
         });
}

namespace {

constexpr absl::string_view kModelVariable = "model";

// `layout` spelled out once per side of its `{unary|stream}` choice, or once if it has none.
absl::StatusOr<std::vector<std::string>> spellOut(absl::string_view layout) {
  const size_t bar = layout.find('|');
  if (bar == absl::string_view::npos) {
    return std::vector<std::string>{std::string(layout)};
  }
  const size_t open = layout.rfind('{', bar);
  const size_t close = layout.find('}', bar);
  if (open == absl::string_view::npos || close == absl::string_view::npos ||
      layout.find('|', bar + 1) != absl::string_view::npos ||
      layout.substr(open + 1, close - open - 1).find_first_of("{}") != absl::string_view::npos) {
    return absl::InvalidArgumentError("expected at most one `{unary|stream}`");
  }
  const absl::string_view before = layout.substr(0, open);
  const absl::string_view after = layout.substr(close + 1);
  return std::vector<std::string>{
      absl::StrCat(before, layout.substr(open + 1, bar - open - 1), after),
      absl::StrCat(before, layout.substr(bar + 1, close - bar - 1), after)};
}

// Whether `path_template`, which parsed, has an unnamed wildcard. Fails on a named variable that
// spans more than one segment, whose value could never be a path token.
absl::StatusOr<bool> hasUnnamedWildcard(absl::string_view path_template) {
  bool wildcard = false;
  while (true) {
    const size_t special = path_template.find_first_of("{*");
    if (special == absl::string_view::npos) {
      return wildcard;
    }
    if (path_template[special] == '*') {
      wildcard = true;
      path_template.remove_prefix(special + 1);
      continue;
    }
    const size_t close = path_template.find('}', special);
    const absl::string_view variable = path_template.substr(special + 1, close - special - 1);
    const size_t equals = variable.find('=');
    if (equals != absl::string_view::npos && variable.substr(equals + 1) != "*") {
      return absl::InvalidArgumentError(absl::StrCat("variable `{", variable.substr(0, equals),
                                                     "}` must match one path segment"));
    }
    path_template.remove_prefix(close + 1);
  }
}

} // namespace

absl::StatusOr<UriPattern::Layout> UriPattern::parseLayout(absl::string_view layout) {
  absl::StatusOr<std::vector<std::string>> texts = spellOut(layout);
  if (!texts.ok()) {
    return texts.status();
  }
  Layout spellings;
  for (absl::string_view text : *texts) {
    const std::pair<absl::string_view, absl::string_view> path_and_query =
        absl::StrSplit(text, absl::MaxSplits('?', 1));
    absl::StatusOr<std::string> regex =
        UriTemplate::convertPathPatternSyntaxToRegex(path_and_query.first);
    if (!regex.ok()) {
      return regex.status();
    }
    absl::StatusOr<bool> wildcard = hasUnnamedWildcard(path_and_query.first);
    if (!wildcard.ok()) {
      return wildcard.status();
    }
    auto compiled = std::make_shared<const RE2>(*regex);
    Spelling spelling{std::string(path_and_query.first), std::string(path_and_query.second),
                      compiled};
    spelling.has_wildcard = *wildcard;
    for (const auto& [index, name] : compiled->CapturingGroupNames()) {
      spelling.has_model |= name == kModelVariable;
      spelling.groups.emplace_back(index, name);
    }
    spellings.push_back(std::move(spelling));
  }
  return spellings;
}

absl::StatusOr<UriPattern> UriPattern::parse(absl::string_view pattern) {
  std::vector<Layout> layouts;
  for (absl::string_view text : absl::StrSplit(pattern, ' ')) {
    absl::StatusOr<Layout> layout = parseLayout(text);
    if (!layout.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("uri pattern `", pattern, "`: ", layout.status().message()));
    }
    layouts.push_back(*std::move(layout));
  }
  return UriPattern(std::string(pattern), std::move(layouts));
}

UriPattern::UriPattern(std::string source, std::vector<Layout> layouts)
    : source_(std::move(source)), layouts_(std::move(layouts)) {
  for (const Spelling& spelling : layouts_.front()) {
    renderable_ &= !spelling.has_wildcard;
  }
  for (const auto& [index, name] : layouts_.front().front().groups) {
    render_variables_.push_back(name);
  }
}

std::optional<UriPattern::Match> UriPattern::matchSpelling(const Spelling& spelling,
                                                           absl::string_view path) {
  std::vector<absl::string_view> captures(spelling.regex->NumberOfCapturingGroups() + 1);
  if (!spelling.regex->Match(path, 0, path.size(), RE2::ANCHOR_BOTH, captures.data(),
                             captures.size())) {
    return std::nullopt;
  }
  Match match;
  for (const auto& [index, name] : spelling.groups) {
    const absl::string_view value = captures[index];
    if (!isPathToken(value)) {
      return std::nullopt;
    }
    if (name == kModelVariable) {
      match.model = std::string(value);
    } else {
      match.variables.emplace(name, value);
    }
  }
  return match;
}

std::optional<UriPattern::Match> UriPattern::match(absl::string_view path) const {
  path = Http::PathUtil::removeQueryAndFragment(path);
  for (const Layout& layout : layouts_) {
    // Unary first: spellings that differ only in their query leave the streaming mode to the body.
    for (size_t i = 0; i < layout.size(); ++i) {
      std::optional<Match> match = matchSpelling(layout[i], path);
      if (match.has_value()) {
        match->stream = i > 0;
        return match;
      }
    }
  }
  return std::nullopt;
}

absl::StatusOr<std::string> UriPattern::render(bool stream, ValueLookup value) const {
  const Layout& layout = layouts_.front();
  const Spelling& spelling = stream ? layout.back() : layout.front();
  std::string path;
  absl::string_view rest = spelling.path;
  while (true) {
    const size_t special = rest.find_first_of("{*");
    absl::StrAppend(&path, rest.substr(0, special));
    if (special == absl::string_view::npos) {
      break;
    }
    if (rest[special] == '*') {
      return absl::FailedPreconditionError(
          absl::StrCat("uri pattern `", source_, "` has a wildcard, which cannot be rendered"));
    }
    // The template parsed, so every `{` is closed and names a variable, optionally `{name=...}`.
    const size_t close = rest.find('}', special);
    const absl::string_view variable = rest.substr(special + 1, close - special - 1);
    const absl::string_view name = variable.substr(0, variable.find('='));
    const std::optional<std::string> filled = value(name);
    if (!filled.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("uri pattern `", source_, "` has no value for `{", name, "}`"));
    }
    if (!isPathToken(*filled)) {
      return absl::InvalidArgumentError(
          absl::StrCat("uri pattern `", source_, "`: `", *filled, "` cannot fill `{", name, "}`"));
    }
    absl::StrAppend(&path, *filled);
    rest = rest.substr(close + 1);
  }
  if (!spelling.query.empty()) {
    absl::StrAppend(&path, "?", spelling.query);
  }
  return path;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
