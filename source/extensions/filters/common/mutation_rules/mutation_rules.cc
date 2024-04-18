#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"

#include "source/common/common/macros.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace MutationRules {

using Http::HeaderUtility;
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

  const absl::flat_hash_set<LowerCaseString>& headers() const { return headers_; }

private:
  absl::flat_hash_set<LowerCaseString> headers_;
};

Checker::Checker(const envoy::config::common::mutation_rules::v3::HeaderMutationRules& rules,
                 Regex::Engine& regex_engine)
    : rules_(rules) {
  if (rules.has_allow_expression()) {
    allow_expression_ = Regex::Utility::parseRegex(rules.allow_expression(), regex_engine);
  }
  if (rules.has_disallow_expression()) {
    disallow_expression_ = Regex::Utility::parseRegex(rules.disallow_expression(), regex_engine);
  }
}

CheckResult Checker::check(CheckOperation op, const LowerCaseString& header_name,
                           absl::string_view header_value) const {
  if (isAllowed(op, header_name) &&
      (op == CheckOperation::REMOVE || isValidValue(header_name, header_value))) {
    return CheckResult::OK;
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_is_error, false)) {
    return CheckResult::FAIL;
  }
  return CheckResult::IGNORE;
}

bool Checker::isAllowed(CheckOperation op, const LowerCaseString& header_name) const {
  if (op == CheckOperation::REMOVE && !HeaderUtility::isModifiableHeader(header_name)) {
    // No matter what, you can't remove the "host" or any ":" headers.
    return false;
  }
  if (disallow_expression_ && disallow_expression_->match(header_name)) {
    // Mutations are always disallowed if they match the expression.
    return false;
  }
  if (allow_expression_ && allow_expression_->match(header_name)) {
    // Mutations are always allowed if they match the expression.
    return true;
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_all, false)) {
    // Mutations are always disallowed if this is true.
    return false;
  }
  if (!PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, allow_all_routing, false) &&
      extraRoutingHeaders().headers().contains(header_name)) {
    // If false, check the pre-defined list of "extra routing headers"
    // and fail if the header is in that list.
    return false;
  }
  if (absl::StartsWith(header_name, ":") &&
      (op == CheckOperation::APPEND ||
       PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, disallow_system, false))) {
    // Disallow changes to system headers if explicitly disallowed, or if
    // if the operation is "append," since system headers don't allow multiple
    // values.
    return false;
  }
  if (!PROTOBUF_GET_WRAPPED_OR_DEFAULT(rules_, allow_envoy, false) &&
      absl::StartsWith(header_name, Http::Headers::get().prefix())) {
    // If false, prevent changes to "x-envoy" headers (or the equivalent).
    return false;
  }
  return true;
}

bool Checker::isValidValue(const LowerCaseString& header_name,
                           absl::string_view header_value) const {
  if (!absl::StartsWith(header_name, ":") && !HeaderUtility::headerValueIsValid(header_value)) {
    // For non-internal headers, make sure that value matches character set.
    return false;
  }
  // Make specific checks for sensitive headers that will cause Envoy to behave
  // badly if set to invalid values.
  const auto& hdrs = Http::Headers::get();
  if ((header_name == hdrs.Host || header_name == hdrs.HostLegacy) &&
      !HeaderUtility::authorityIsValid(header_value)) {
    return false;
  }
  if (header_name == hdrs.Scheme && !Http::Utility::schemeIsValid(header_value)) {
    return false;
  }
  if (header_name == hdrs.Status) {
    uint32_t status;
    if (!absl::SimpleAtoi(header_value, &status)) {
      // :status is not actually a number.
      return false;
    }
    if (status < 200) {
      // :status is not valid value (likely 0).
      return false;
    }
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
