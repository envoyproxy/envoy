#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include <algorithm>
#include <utility>

#include "source/common/http/path_utility.h"
#include "source/extensions/path/uri_template_lib/uri_template.h"

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

bool isModelId(absl::string_view model) {
  return !model.empty() && std::all_of(model.begin(), model.end(), [](char c) {
    return absl::ascii_isalnum(c) || c == '-' || c == '.' || c == '_' || c == '@';
  });
}

namespace {

constexpr absl::string_view kModelVariable = "model";
constexpr absl::string_view kModelPlaceholder = "{model}";

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
    auto compiled = std::make_shared<const RE2>(*regex);
    const auto& groups = compiled->NamedCapturingGroups();
    const auto model = groups.find(std::string(kModelVariable));
    spellings.push_back({std::string(path_and_query.first), std::string(path_and_query.second),
                         std::move(compiled), model != groups.end() ? model->second : 0});
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

std::optional<UriPattern::Match> UriPattern::match(absl::string_view path) const {
  path = Http::PathUtil::removeQueryAndFragment(path);
  for (const Layout& layout : layouts_) {
    // Unary first: spellings that differ only in their query leave the streaming mode to the body.
    for (size_t i = 0; i < layout.size(); ++i) {
      const Spelling& spelling = layout[i];
      const bool has_model = spelling.model_group > 0;
      // Reads groups up to the model's: the last read, unless it is group 0, the whole path.
      std::vector<absl::string_view> groups(spelling.model_group + 1);
      if (spelling.regex->Match(path, 0, path.size(), RE2::ANCHOR_BOTH, groups.data(),
                                groups.size()) &&
          (!has_model || isModelId(groups.back()))) {
        return Match{has_model ? std::string(groups.back()) : "", i > 0};
      }
    }
  }
  return std::nullopt;
}

absl::StatusOr<std::string> UriPattern::render(absl::string_view model, bool stream) const {
  if (hasModel() && !isModelId(model)) {
    return absl::InvalidArgumentError(absl::StrCat("uri pattern `", source_,
                                                   "` names the model in the path, so `", model,
                                                   "` must be a model id"));
  }
  const Layout& layout = layouts_.front();
  const Spelling& spelling = stream ? layout.back() : layout.front();
  std::string path = absl::StrReplaceAll(spelling.path, {{kModelPlaceholder, model}});
  if (path.find_first_of("{*") != std::string::npos) {
    return absl::InvalidArgumentError(
        absl::StrCat("uri pattern `", source_, "` has segments other than `{model}` to fill"));
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
