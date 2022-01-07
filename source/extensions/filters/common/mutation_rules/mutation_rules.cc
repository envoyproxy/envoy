#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"

#include "envoy/http/header_map.h"

#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace MutationRules {

Checker::Checker(const envoy::config::common::mutation_rules::v3::HeaderMutationRules& rules)
    : rules_(rules) {
  if (rules.has_allow_expression()) {
    allow_expression_ = Regex::Utility::parseRegex(rules.allow_expression());
  }
  if (rules.has_disallow_expression()) {
    disallow_expression_ = Regex::Utility::parseRegex(rules.disallow_expression());
  }
}

// Pre-populate "routing headers" that we'll use to match whether a header
// should be rejected according to the "allow_routing" rule.
absl::flat_hash_set<Http::LowerCaseString> Checker::createRoutingHeaders() {
  absl::flat_hash_set<Http::LowerCaseString> routing_headers;
  const auto& headers = Http::Headers::get();
  routing_headers.insert(headers.HostLegacy);
  routing_headers.insert(headers.Host);
  routing_headers.insert(headers.Method);
  routing_headers.insert(headers.Scheme);
  return routing_headers;
}

const absl::flat_hash_set<Http::LowerCaseString>& Checker::getRoutingHeaders() {
  static absl::flat_hash_set<Http::LowerCaseString> routing_headers = createRoutingHeaders();
  return routing_headers;
}

CheckResult Checker::check(absl::string_view header_name) const {
  if (isAllowed(header_name)) {
    return CheckResult::OK;
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_is_error, false)) {
    return CheckResult::FAIL;
  }
  return CheckResult::IGNORE;
}

bool Checker::isAllowed(absl::string_view header_name) const {
  const Http::LowerCaseString lower_name(header_name);
  if (disallow_expression_ && disallow_expression_->match(lower_name)) {
    // Mutations are always disallowed if they match the expression.
    return false;
  }
  if (allow_expression_ && allow_expression_->match(lower_name)) {
    // Mutations are always allowed if they match the expression.
    return true;
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_all, false)) {
    // Mutations are always disallowed if this is true.
    return false;
  }
  if (!PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, allow_all_routing, false) &&
      getRoutingHeaders().contains(lower_name)) {
    // Unless this is true, modifications to headers in the of the
    // "routing_headers" map above must be rejected.
    return false;
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_system, false) &&
      absl::StartsWith(lower_name, ":")) {
    // If true, disallow changes to all internal headers.
    return false;
  }
  if (!PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, allow_envoy, false) &&
      absl::StartsWith(lower_name, Http::Headers::get().prefix())) {
    // If true, disallow changes to "x-envoy" headers (or the equivalent).
    return false;
  }
  return true;
}

} // namespace MutationRules
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
