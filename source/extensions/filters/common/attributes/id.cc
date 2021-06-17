
#include "source/extensions/filters/common/attributes/id.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

/// Returns absl::nullopt if the path was invalid
absl::optional<AttributeId> AttributeId::from_path(absl::string_view path) {
  RootToken root;
  absl::optional<SubToken> sub;
  // ex: "request.foobar"
  //             ^

  // Get the root token
  size_t root_token_str_end = std::min(path.find('.'), path.find('\0'));
  if (root_token_str_end == absl::string_view::npos) {
    root_token_str_end = path.size();
  }

  auto root_tok = path.substr(0, root_token_str_end);
  auto root_part = root_tokens.find(root_tok);
  if (root_part != root_tokens.end()) {
    root = root_part->second;
  } else {
    return absl::nullopt;
  }

  absl::string_view sub_tok;
  if (root_token_str_end + 1 < path.size()) {
    sub_tok = path.substr(root_token_str_end + 1,
                          std::min(path.find('\0'), path.size() - root_token_str_end - 1));
  }

  // Validate root and sub token are a valid pair
  switch (root) {
  case RootToken::METADATA:
    if (sub_tok.size() != 0) {
      return absl::nullopt;
    }
    break;
  case RootToken::FILTER_STATE:
    if (sub_tok.size() != 0) {
      return absl::nullopt;
    }
    break;
  case RootToken::REQUEST: {
    auto part = request_tokens.find(sub_tok);
    if (part != request_tokens.end()) {
      sub = absl::make_optional(part->second);
    } else {
      return absl::nullopt;
    }
    break;
  }
  case RootToken::RESPONSE: {
    auto part = response_tokens.find(sub_tok);
    if (part != response_tokens.end()) {
      sub = absl::make_optional(part->second);
    } else {
      return absl::nullopt;
    }
    break;
  }
  case RootToken::SOURCE: {
    auto part = source_tokens.find(sub_tok);
    if (part != source_tokens.end()) {
      sub = absl::make_optional(part->second);
    } else {
      return absl::nullopt;
    }
    break;
  }
  case RootToken::DESTINATION: {
    auto part = destination_tokens.find(sub_tok);
    if (part != destination_tokens.end()) {
      sub = absl::make_optional(part->second);
    } else {
      return absl::nullopt;
    }
    break;
  }
  case RootToken::CONNECTION: {
    auto part = connection_tokens.find(sub_tok);
    if (part != connection_tokens.end()) {
      sub = absl::make_optional(part->second);
    } else {
      return absl::nullopt;
    }
    break;
  }
  case RootToken::UPSTREAM: {
    auto part = upstream_tokens.find(sub_tok);
    if (part != upstream_tokens.end()) {
      sub = absl::make_optional(part->second);
    } else {
      return absl::nullopt;
    }
    break;
  }
  }
  return absl::make_optional(AttributeId(root, sub));
}
} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy