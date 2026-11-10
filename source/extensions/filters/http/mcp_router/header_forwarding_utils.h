#pragma once

#include <memory>
#include <vector>

#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/matchers.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

// Per-backend policy controlling which downstream request headers are forwarded to an MCP
// backend. See headerCanBeForwarded() for the evaluation order.
struct HeaderForwardingPolicy {
  bool forward_all = false;
  std::vector<Matchers::StringMatcherPtr> allowed_headers;
  std::vector<Matchers::StringMatcherPtr> disallowed_headers;
};

// Builds matcher objects from a ListStringMatcher proto's patterns.
std::vector<Matchers::StringMatcherPtr>
initHeaderMatchers(const envoy::type::matcher::v3::ListStringMatcher& header_list,
                   Server::Configuration::CommonFactoryContext& context);

// Parses a HeaderForwarding-shaped proto message (referenced from a McpBackend's
// header_forwarding field) into a HeaderForwardingPolicy. Templated because
// envoy.extensions.filters.http.mcp_router.v3.McpRouter.HeaderForwarding and
// envoy.extensions.clusters.mcp_multicluster.v3.ClusterConfig.HeaderForwarding are
// independent, field-for-field-identical proto types (mirroring how McpBackend itself is
// duplicated across the two proto files), not a shared message.
template <typename HeaderForwardingProto>
HeaderForwardingPolicy parseHeaderForwarding(const HeaderForwardingProto& proto,
                                             Server::Configuration::CommonFactoryContext& context) {
  HeaderForwardingPolicy policy;
  policy.forward_all = proto.forward_all();
  policy.allowed_headers = initHeaderMatchers(proto.allowed_headers(), context);
  policy.disallowed_headers = initHeaderMatchers(proto.disallowed_headers(), context);
  return policy;
}

// Decides whether a single header may be forwarded to the backend under the given policy.
//
// Evaluation order:
//   1. If the header matches disallowed_headers, it is never forwarded -- this takes
//      precedence over everything below, including forward_all.
//   2. Otherwise, if forward_all is true, the header is forwarded.
//   3. Otherwise, if the header matches allowed_headers, it is forwarded.
//   4. Otherwise, the header is not forwarded.
//
// This deliberately inverts ext_proc's HeaderForwardingRules default (empty allow list means
// forward-everything-not-denied there): here, an unset/empty policy forwards nothing, because
// MCP requires audience-bound tokens and prohibits implicit token passthrough to backends the
// client never authenticated to.
bool headerCanBeForwarded(absl::string_view header_key, const HeaderForwardingPolicy& policy);

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
