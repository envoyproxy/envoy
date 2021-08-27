#include "source/extensions/filters/common/attributes/id.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

absl::optional<absl::string_view> AttributeId::subName() {
  if (!sub_token_) {
    return absl::nullopt;
  }
  switch (root_token_) {
  case RootToken::REQUEST:
    return request_tokens_inv[absl::get<RequestToken>(*sub_token_)];
  case RootToken::RESPONSE:
    return response_tokens_inv[absl::get<ResponseToken>(*sub_token_)];
  case RootToken::SOURCE:
    return source_tokens_inv[absl::get<SourceToken>(*sub_token_)];
  case RootToken::DESTINATION:
    return destination_tokens_inv[absl::get<DestinationToken>(*sub_token_)];
  case RootToken::CONNECTION:
    return connection_tokens_inv[absl::get<ConnectionToken>(*sub_token_)];
  case RootToken::UPSTREAM:
    return upstream_tokens_inv[absl::get<UpstreamToken>(*sub_token_)];
  default:
    // metadata and filter_state do not have sub-tokens.
    return absl::nullopt;
  }
};

bool AttributeId::sub(RequestToken& tok) {
  if (root() == RootToken::REQUEST && sub_token_) {
    if (auto val = absl::get_if<RequestToken>(&*sub_token_)) {
      tok = *val;
      return true;
    }
  }
  return false;
}
bool AttributeId::sub(ResponseToken& tok) {
  if (root() == RootToken::RESPONSE && sub_token_) {
    if (auto val = absl::get_if<ResponseToken>(&*sub_token_)) {
      tok = *val;
      return true;
    }
  }
  return false;
}
bool AttributeId::sub(SourceToken& tok) {
  if (root() == RootToken::SOURCE && sub_token_) {
    if (auto val = absl::get_if<SourceToken>(&*sub_token_)) {
      tok = *val;
      return true;
    }
  }
  return false;
}
bool AttributeId::sub(DestinationToken& tok) {
  if (root() == RootToken::DESTINATION && sub_token_) {
    if (auto val = absl::get_if<DestinationToken>(&*sub_token_)) {
      tok = *val;
      return true;
    }
  }
  return false;
}
bool AttributeId::sub(ConnectionToken& tok) {
  if (root() == RootToken::CONNECTION && sub_token_) {
    if (auto val = absl::get_if<ConnectionToken>(&*sub_token_)) {
      tok = *val;
      return true;
    }
  }
  return false;
}
bool AttributeId::sub(UpstreamToken& tok) {
  if (root() == RootToken::UPSTREAM && sub_token_) {
    if (auto val = absl::get_if<UpstreamToken>(&*sub_token_)) {
      tok = *val;
      return true;
    }
  }
  return false;
}

/// Returns absl::nullopt if the path was invalid
absl::optional<AttributeId> AttributeId::fromPath(absl::string_view path) {
  RootToken root;
  absl::optional<SubToken> sub;

  size_t root_token_str_end = std::min(path.find('.'), path.find('\0'));
  {
    // Get the root token
    if (root_token_str_end == absl::string_view::npos) {
      root_token_str_end = path.size();
    }

    auto root_tok_str = path.substr(0, root_token_str_end);
    auto root_part = root_tokens.find(root_tok_str);
    if (root_part == root_tokens.end()) {
      return absl::nullopt;
    }
    root = root_part->second;
  }

  {
    absl::string_view sub_tok_str;
    if (root_token_str_end == path.size()) {
      return absl::make_optional(AttributeId(root, absl::nullopt));
    } else if (root_token_str_end > path.size()) {
      return absl::nullopt;
    } else {
    }
    sub_tok_str = path.substr(root_token_str_end + 1,
                              std::min(path.find('\0'), path.size() - root_token_str_end - 1));

    sub = parseSubToken(root, sub_tok_str);
    if (sub == absl::nullopt) {
      return absl::nullopt;
    }
  }
  return absl::make_optional(AttributeId(root, sub));
}

// A return value of absl::nullopt that indicates that the `sub_tok_str` is invalid and
// therefore the entire attribute string is invalid
absl::optional<SubToken> AttributeId::parseSubToken(RootToken root, absl::string_view sub_tok_str) {
  // Validate root and sub token are a valid pair.
  // For metadata and filter_state they will always be null.
  switch (root) {
  case RootToken::METADATA:
  case RootToken::FILTER_STATE:
    break;
  case RootToken::REQUEST: {
    auto part = request_tokens.find(sub_tok_str);
    if (part != request_tokens.end()) {
      return absl::make_optional(part->second);
    }
    break;
  }
  case RootToken::RESPONSE: {
    auto part = response_tokens.find(sub_tok_str);
    if (part != response_tokens.end()) {
      return absl::make_optional(part->second);
    }
    break;
  }
  case RootToken::SOURCE: {
    auto part = source_tokens.find(sub_tok_str);
    if (part != source_tokens.end()) {
      return absl::make_optional(part->second);
    }
    break;
  }
  case RootToken::DESTINATION: {
    auto part = destination_tokens.find(sub_tok_str);
    if (part != destination_tokens.end()) {
      return absl::make_optional(part->second);
    }
    break;
  }
  case RootToken::CONNECTION: {
    auto part = connection_tokens.find(sub_tok_str);
    if (part != connection_tokens.end()) {
      return absl::make_optional(part->second);
    }
    break;
  }
  case RootToken::UPSTREAM: {
    auto part = upstream_tokens.find(sub_tok_str);
    if (part != upstream_tokens.end()) {
      return absl::make_optional(part->second);
    }
    break;
  }
  }
  // `sub_tok_str` was invalid
  return absl::nullopt;
}
} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
