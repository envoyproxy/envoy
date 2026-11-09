#include "source/extensions/filters/common/rbac/utility.h"

#include <string>

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

static absl::Status validateFail(const std::string& header) {
  return absl::InvalidArgumentError(fmt::format("Found header({}) rule,"
                                                "not supported by RBAC network filter",
                                                header));
}

static absl::Status validatePermission(const envoy::config::rbac::v3::Permission& permission) {
  if (permission.has_header()) {
    return validateFail(permission.header().DebugString());
  }
  if (permission.has_and_rules()) {
    for (const auto& r : permission.and_rules().rules()) {
      RETURN_IF_NOT_OK(validatePermission(r));
    }
  }
  if (permission.has_or_rules()) {
    for (const auto& r : permission.or_rules().rules()) {
      RETURN_IF_NOT_OK(validatePermission(r));
    }
  }
  if (permission.has_not_rule()) {
    RETURN_IF_NOT_OK(validatePermission(permission.not_rule()));
  }
  return absl::OkStatus();
}

static absl::Status validatePrincipal(const envoy::config::rbac::v3::Principal& principal) {
  if (principal.has_header()) {
    return validateFail(principal.header().DebugString());
  }
  if (principal.has_and_ids()) {
    for (const auto& r : principal.and_ids().ids()) {
      RETURN_IF_NOT_OK(validatePrincipal(r));
    }
  }
  if (principal.has_or_ids()) {
    for (const auto& r : principal.or_ids().ids()) {
      RETURN_IF_NOT_OK(validatePrincipal(r));
    }
  }
  if (principal.has_not_id()) {
    RETURN_IF_NOT_OK(validatePrincipal(principal.not_id()));
  }
  return absl::OkStatus();
}

absl::Status validateRbacRules(const envoy::config::rbac::v3::RBAC& rules) {
  for (const auto& policy : rules.policies()) {
    for (const auto& permission : policy.second.permissions()) {
      RETURN_IF_NOT_OK(validatePermission(permission));
    }
    for (const auto& principal : policy.second.principals()) {
      RETURN_IF_NOT_OK(validatePrincipal(principal));
    }
  }
  return absl::OkStatus();
}

RoleBasedAccessControlFilterStats generateStats(const std::string& prefix,
                                                const std::string& rules_prefix,
                                                const std::string& shadow_rules_prefix,
                                                Stats::Scope& scope) {
  const std::string base_prefix = Envoy::statPrefixJoin(prefix, "rbac.");
  const std::string final_rules_prefix = Envoy::statPrefixJoin(base_prefix, rules_prefix);
  const std::string per_policy_final_rules_prefix =
      Envoy::statPrefixJoin(final_rules_prefix, "policy.");
  const std::string final_shadow_rules_prefix =
      Envoy::statPrefixJoin(base_prefix, shadow_rules_prefix);
  const std::string per_policy_final_shadow_rules_prefix =
      Envoy::statPrefixJoin(final_shadow_rules_prefix, "policy.");
  return {
      ENFORCE_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_rules_prefix))
          SHADOW_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_shadow_rules_prefix)) scope,
      per_policy_final_rules_prefix,
      per_policy_final_shadow_rules_prefix,
  };
}

std::string responseDetail(absl::string_view policy_id) {
  // Replace whitespaces in policy_id with '_' to avoid breaking the access log (inconsistent number
  // of segments between log entries when the separator is whitespace).
  const std::string sanitized = StringUtil::replaceAllEmptySpace(policy_id);
  return fmt::format("rbac_access_denied_matched_policy[{}]", sanitized);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
