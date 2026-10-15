#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// True when `value` can fill a path segment: it is non-empty, starts with a letter or a digit, and
// holds only characters that cannot escape a segment.
bool isPathToken(absl::string_view value);

// Where an API's request path carries request values: one or more layouts, separated by spaces,
// each a URI template in the route's `path_match_policy` syntax with at most one `{unary|stream}`
// choice between its two spellings, e.g.:
//   /v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse}
//
// A path matches the first layout that fits all of it but the query, and each named variable,
// which matches one segment, reads a value from it. `render()` writes the first layout. A spelling
// may end in a query, which `render()` appends and `match()` ignores. Unnamed wildcards match
// segments `render()` cannot fill.
class UriPattern {
public:
  // What a matched path names.
  struct Match {
    // `{model}`; empty when the matched layout has none.
    std::string model;
    bool stream{false};
    // Every other named variable, such as `{project}`.
    absl::flat_hash_map<std::string, std::string> variables;
  };
  // The value of the variable `name`; nullopt when there is none.
  using ValueLookup = absl::FunctionRef<std::optional<std::string>(absl::string_view name)>;

  static absl::StatusOr<UriPattern> parse(absl::string_view pattern);

  // nullopt when no layout fits `path`, or one of its values is not a path token.
  std::optional<Match> match(absl::string_view path) const;

  // The first layout spelled for `stream`, each variable filled by `value`. Fails with
  // `kFailedPrecondition` on a variable without a value or an unnamed wildcard, and with
  // `kInvalidArgument` on a value that is not a path token.
  absl::StatusOr<std::string> render(bool stream, ValueLookup value) const;

  // Whether the layout `render()` writes names the model.
  bool hasModel() const { return layouts_.front().front().has_model; }
  // The named variables of the layout `render()` writes.
  const std::vector<std::string>& renderVariables() const { return render_variables_; }
  // Whether that layout has no unnamed wildcard, which `render()` cannot fill.
  bool renderable() const { return renderable_; }
  // The pattern as parsed.
  const std::string& source() const { return source_; }

private:
  // One side of a layout's choice: a URI template, and the query rendered after it.
  struct Spelling {
    std::string path;
    std::string query;
    std::shared_ptr<const RE2> regex;
    // The named capture groups, by index.
    std::vector<std::pair<int, std::string>> groups;
    bool has_model{false};
    bool has_wildcard{false};
  };
  // The unary spelling, then the streaming one when the layout spells it differently.
  using Layout = std::vector<Spelling>;

  static absl::StatusOr<Layout> parseLayout(absl::string_view layout);
  static std::optional<Match> matchSpelling(const Spelling& spelling, absl::string_view path);

  UriPattern(std::string source, std::vector<Layout> layouts);

  std::string source_;
  std::vector<Layout> layouts_;
  std::vector<std::string> render_variables_;
  bool renderable_{true};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
