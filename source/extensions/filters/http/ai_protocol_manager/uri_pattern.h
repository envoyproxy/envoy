#pragma once

#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// True when `model` can be a path segment: it is non-empty and holds only characters that cannot
// escape one.
bool isModelId(absl::string_view model);

// A request path template used to extract (`match()`) or rewrite (`render()`) the model and
// streaming mode in URL paths. Supports `{model}` and `{unary|stream}` placeholders, e.g.:
//   /v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse}
//
// The streaming mode is spelled out as two literal alternatives (either may be empty) rather than
// a `{method}` placeholder so the pattern stays dialect-agnostic without per-dialect method tables.
class UriPattern {
public:
  // What a matched path names. `model` is empty when the pattern has no `{model}`.
  struct Match {
    std::string model;
    bool stream{false};
  };

  // Parses `pattern`, or returns an error if it is empty or has invalid placeholders.
  static absl::StatusOr<UriPattern> parse(absl::string_view pattern);

  // Matches `path` (ignoring query parameters; a leading literal only has to match by its last
  // segment, so a gateway or Vertex AI prefix ahead of the API's own path does not hide it) and
  // extracts the model and streaming mode, or returns `std::nullopt` if it does not match.
  std::optional<Match> match(absl::string_view path) const;

  // Renders the pattern for `model` and `stream`. Fails when the pattern has `{model}` and `model`
  // is not a model id.
  absl::StatusOr<std::string> render(absl::string_view model, bool stream) const;

  bool hasModel() const { return has_model_; }
  // The pattern as parsed.
  const std::string& source() const { return source_; }

private:
  struct Piece {
    enum class Kind { Literal, Model, Alternative };
    Kind kind{Kind::Literal};
    // Literal: the text. Alternative: the unary text.
    std::string text{};
    // Alternative: the stream text.
    std::string stream_text{};
  };

  UriPattern(std::string source, std::vector<Piece> pieces, bool has_model)
      : source_(std::move(source)), pieces_(std::move(pieces)), has_model_(has_model) {}

  // Matches the pieces from `first` on against `path` from `pos` on, to its end.
  std::optional<Match> matchFrom(size_t first, absl::string_view path, size_t pos) const;

  std::string source_;
  std::vector<Piece> pieces_;
  bool has_model_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
