#include "source/extensions/filters/http/mcp_router/header_forwarding_utils.h"

#include <algorithm>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

std::vector<Matchers::StringMatcherPtr>
initHeaderMatchers(const envoy::type::matcher::v3::ListStringMatcher& header_list,
                   Server::Configuration::CommonFactoryContext& context) {
  std::vector<Matchers::StringMatcherPtr> header_matchers;
  header_matchers.reserve(header_list.patterns_size());
  for (const auto& matcher : header_list.patterns()) {
    header_matchers.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher, context));
  }
  return header_matchers;
}

namespace {
bool matchesAny(absl::string_view key, const std::vector<Matchers::StringMatcherPtr>& matchers) {
  return std::any_of(
      matchers.begin(), matchers.end(),
      [key](const Matchers::StringMatcherPtr& matcher) { return matcher->match(key); });
}
} // namespace

bool headerCanBeForwarded(absl::string_view header_key, const HeaderForwardingPolicy& policy) {
  if (matchesAny(header_key, policy.disallowed_headers)) {
    return false;
  }
  if (policy.forward_all) {
    return true;
  }
  return matchesAny(header_key, policy.allowed_headers);
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
