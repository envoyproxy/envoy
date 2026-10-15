#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// True when `model` can be a path segment: it is non-empty and holds only characters that cannot
// escape one.
bool isModelId(absl::string_view model);

// Where an API names the model and the streaming mode in its request path: one or more layouts,
// separated by spaces, each a URI template in the route's `path_match_policy` syntax with at most
// one `{unary|stream}` choice between its two spellings, e.g.:
//   /v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse}
//
// A path matches the first layout that fits all of it but the query; `render()` writes the first
// layout. A spelling may end in a query, which `render()` appends and `match()` ignores. Variables
// other than `{model}`, and wildcards, match segments that `render()` cannot fill.
class UriPattern {
public:
  // What a matched path names. `model` is empty when the matched layout has no `{model}`.
  struct Match {
    std::string model;
    bool stream{false};
  };

  static absl::StatusOr<UriPattern> parse(absl::string_view pattern);

  std::optional<Match> match(absl::string_view path) const;

  // The path for `model` and `stream`. Fails when the layout has `{model}` and `model` is not a
  // model id, or has segments that `{model}` does not fill.
  absl::StatusOr<std::string> render(absl::string_view model, bool stream) const;

  // Whether the layout `render()` writes names the model.
  bool hasModel() const { return layouts_.front().front().model_group > 0; }
  // The pattern as parsed.
  const std::string& source() const { return source_; }

private:
  // One side of a layout's choice: a URI template, and the query rendered after it.
  struct Spelling {
    std::string path;
    std::string query;
    std::shared_ptr<const RE2> regex;
    // The regex group that captures `{model}`; 0, the whole match, when there is none.
    int model_group{0};
  };
  // The unary spelling, then the streaming one when the layout spells it differently.
  using Layout = std::vector<Spelling>;

  static absl::StatusOr<Layout> parseLayout(absl::string_view layout);

  UriPattern(std::string source, std::vector<Layout> layouts)
      : source_(std::move(source)), layouts_(std::move(layouts)) {}

  std::string source_;
  std::vector<Layout> layouts_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
