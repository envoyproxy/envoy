#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

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

constexpr absl::string_view kModelPlaceholder = "model";

// The text an alternative is matched by: without the query a rendered path adds.
absl::string_view withoutQuery(absl::string_view text) { return text.substr(0, text.find('?')); }

// The last segment of a literal that precedes the model, slashes included: `/models/` of
// `/v1beta/models/`. The literal itself when it has no earlier slash.
absl::string_view lastSegment(absl::string_view literal) {
  const size_t slash = absl::StripSuffix(literal, "/").rfind('/');
  return slash == absl::string_view::npos ? literal : literal.substr(slash);
}

} // namespace

absl::StatusOr<UriPattern> UriPattern::parse(absl::string_view pattern) {
  if (pattern.empty()) {
    return absl::InvalidArgumentError("uri pattern: empty");
  }
  std::vector<Piece> pieces;
  bool has_model = false;
  bool has_alternative = false;
  size_t pos = 0;
  while (pos < pattern.size()) {
    const size_t open = pattern.find('{', pos);
    if (open == absl::string_view::npos) {
      pieces.push_back(Piece{Piece::Kind::Literal, std::string(pattern.substr(pos))});
      break;
    }
    if (open > pos) {
      pieces.push_back(Piece{Piece::Kind::Literal, std::string(pattern.substr(pos, open - pos))});
    }
    const size_t close = pattern.find('}', open);
    if (close == absl::string_view::npos) {
      return absl::InvalidArgumentError(
          absl::StrCat("uri pattern: unclosed `{` in `", pattern, "`"));
    }
    const absl::string_view placeholder = pattern.substr(open + 1, close - open - 1);
    if (placeholder.find('{') != absl::string_view::npos) {
      return absl::InvalidArgumentError(absl::StrCat("uri pattern: nested `{` in `", pattern, "`"));
    }
    if (placeholder == kModelPlaceholder) {
      if (has_model) {
        return absl::InvalidArgumentError(
            absl::StrCat("uri pattern: more than one `{model}` in `", pattern, "`"));
      }
      has_model = true;
      pieces.push_back(Piece{Piece::Kind::Model});
    } else if (const size_t bar = placeholder.find('|'); bar != absl::string_view::npos) {
      if (has_alternative) {
        return absl::InvalidArgumentError(
            absl::StrCat("uri pattern: more than one `{unary|stream}` in `", pattern, "`"));
      }
      if (placeholder.find('|', bar + 1) != absl::string_view::npos) {
        return absl::InvalidArgumentError(absl::StrCat(
            "uri pattern: `{unary|stream}` takes exactly two alternatives in `", pattern, "`"));
      }
      has_alternative = true;
      pieces.push_back(Piece{Piece::Kind::Alternative, std::string(placeholder.substr(0, bar)),
                             std::string(placeholder.substr(bar + 1))});
    } else {
      return absl::InvalidArgumentError(absl::StrCat("uri pattern: unknown placeholder `{",
                                                     placeholder, "}` in `", pattern,
                                                     "`; expected {model} or {unary|stream}"));
    }
    pos = close + 1;
  }
  // The model ends where the literal after it starts; without one there is no way to tell.
  for (size_t i = 0; i < pieces.size(); ++i) {
    if (pieces[i].kind == Piece::Kind::Model && i + 1 < pieces.size() &&
        pieces[i + 1].kind != Piece::Kind::Literal) {
      return absl::InvalidArgumentError(
          absl::StrCat("uri pattern: `{model}` must be followed by a literal in `", pattern, "`"));
    }
  }
  return UriPattern(std::string(pattern), std::move(pieces), has_model);
}

std::optional<UriPattern::Match> UriPattern::match(absl::string_view path) const {
  path = withoutQuery(path);
  if (pieces_.empty()) {
    return std::nullopt;
  }
  if (pieces_.front().kind != Piece::Kind::Literal) {
    return matchFrom(0, path, 0);
  }
  // A leading literal anchors by its last segment, so a prefix in front of it does not hide the
  // path. Try the latest occurrence first: the model cannot hold a slash, so the anchor that
  // precedes it is the last one before the model.
  const absl::string_view anchor = lastSegment(pieces_.front().text);
  size_t at = path.rfind(anchor);
  while (at != absl::string_view::npos) {
    if (std::optional<Match> matched = matchFrom(1, path, at + anchor.size());
        matched.has_value()) {
      return matched;
    }
    if (at == 0) {
      break;
    }
    at = path.rfind(anchor, at - 1);
  }
  return std::nullopt;
}

std::optional<UriPattern::Match> UriPattern::matchFrom(size_t first, absl::string_view path,
                                                       size_t pos) const {
  Match matched;
  for (size_t i = first; i < pieces_.size(); ++i) {
    const Piece& piece = pieces_[i];
    absl::string_view rest = path.substr(pos);
    switch (piece.kind) {
    case Piece::Kind::Literal:
      if (!absl::StartsWith(rest, piece.text)) {
        return std::nullopt;
      }
      pos += piece.text.size();
      break;
    case Piece::Kind::Model: {
      // Up to the literal that follows, or the end. See parse().
      const size_t end = i + 1 < pieces_.size() ? rest.find(pieces_[i + 1].text) : rest.size();
      if (end == absl::string_view::npos || end == 0 ||
          rest.substr(0, end).find('/') != absl::string_view::npos) {
        return std::nullopt;
      }
      matched.model = std::string(rest.substr(0, end));
      pos += end;
      break;
    }
    case Piece::Kind::Alternative: {
      // The stream side first: it commonly extends the unary one (`streamGenerateContent` does not,
      // but `converse-stream` extends `converse`), and a prefix match must take the longer.
      // TODO(ginama): an API that signals streaming only through the query, with the same method
      // and path for both, cannot be told apart here: the query is ignored, so such a pattern
      // always matches as streaming. No supported model does this yet; look at the request's query
      // when one does.
      const absl::string_view stream = withoutQuery(piece.stream_text);
      const absl::string_view unary = withoutQuery(piece.text);
      if (stream.size() >= unary.size() && absl::StartsWith(rest, stream)) {
        matched.stream = true;
        pos += stream.size();
      } else if (absl::StartsWith(rest, unary)) {
        matched.stream = false;
        pos += unary.size();
      } else if (absl::StartsWith(rest, stream)) {
        matched.stream = true;
        pos += stream.size();
      } else {
        return std::nullopt;
      }
      break;
    }
    }
  }
  if (pos != path.size()) {
    return std::nullopt;
  }
  return matched;
}

absl::StatusOr<std::string> UriPattern::render(absl::string_view model, bool stream) const {
  if (has_model_ && !isModelId(model)) {
    return absl::InvalidArgumentError(absl::StrCat("uri pattern `", source_,
                                                   "` names the model in the path, so `", model,
                                                   "` must be a model id"));
  }
  std::string path;
  for (const Piece& piece : pieces_) {
    switch (piece.kind) {
    case Piece::Kind::Literal:
      absl::StrAppend(&path, piece.text);
      break;
    case Piece::Kind::Model:
      absl::StrAppend(&path, model);
      break;
    case Piece::Kind::Alternative:
      absl::StrAppend(&path, stream ? piece.stream_text : piece.text);
      break;
    }
  }
  return path;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
