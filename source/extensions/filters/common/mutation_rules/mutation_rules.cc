#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"

#include "envoy/http/header_map.h"

#include "source/common/common/macros.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace MutationRules {

using Http::LowerCaseString;

class ExtraRoutingHeaders {
public:
  ExtraRoutingHeaders() {
    const auto& hdrs = Http::Headers::get();
    headers_.insert(hdrs.HostLegacy);
    headers_.insert(hdrs.Host);
    headers_.insert(hdrs.Method);
    headers_.insert(hdrs.Scheme);
  }

  bool containsHeader(const LowerCaseString& name) const { return headers_.contains(name); }

private:
  absl::flat_hash_set<LowerCaseString> headers_;
};

Checker::Checker(const envoy::config::common::mutation_rules::v3::HeaderMutationRules& rules)
    : rules_(rules) {
  if (rules.has_allow_expression()) {
    allow_expression_ = Regex::Utility::parseRegex(rules.allow_expression());
  }
  if (rules.has_disallow_expression()) {
    disallow_expression_ = Regex::Utility::parseRegex(rules.disallow_expression());
  }
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
  const LowerCaseString lower_name(header_name);
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
      extraRoutingHeaders().containsHeader(lower_name)) {
    // If false, check the pre-defined list of "extra routing headers"
    // and fail if the header is in that list.
    return false;
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_system, false) &&
      absl::StartsWith(lower_name, ":")) {
    // If true, disallow changes to all internal headers.
    return false;
  }
  if (!PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, allow_envoy, false) &&
      absl::StartsWith(lower_name, Http::Headers::get().prefix())) {
    // If false, prevent changes to "x-envoy" headers (or the equivalent).
    return false;
  }
  return true;
}

const ExtraRoutingHeaders& Checker::extraRoutingHeaders() {
  CONSTRUCT_ON_FIRST_USE(ExtraRoutingHeaders);
}

} // namespace MutationRules
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
