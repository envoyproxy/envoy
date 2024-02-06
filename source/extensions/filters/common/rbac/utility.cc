#include "source/extensions/filters/common/rbac/utility.h"

#include <string>

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlFilterStats generateStats(const std::string& prefix,
                                                const std::string& rules_prefix,
                                                const std::string& shadow_rules_prefix,
                                                Stats::Scope& scope) {
  const std::string final_prefix = Envoy::statPrefixJoin(prefix + "rbac.", rules_prefix);
  const std::string per_policy_final_prefix = Envoy::statPrefixJoin(final_prefix, "policy.");
  const std::string final_shadow_prefix = Envoy::statPrefixJoin(final_prefix, shadow_rules_prefix);
  const std::string per_policy_final_shadow_prefix = Envoy::statPrefixJoin(final_shadow_prefix, "policy.");

  Stats::StatNameDynamicPool pool(scope.symbolTable());
  const Stats::StatName per_policy_stat(pool.add(per_policy_final_prefix));
  const Stats::StatName per_policy_shadow_stat(pool.add(per_policy_final_shadow_prefix));

  return {
      ENFORCE_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))
      SHADOW_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_shadow_prefix))
      scope,
      std::move(per_policy_stat),
      std::move(per_policy_shadow_stat),
      std::move(pool),
  };
}

std::string responseDetail(const std::string& policy_id) {
  // Replace whitespaces in policy_id with '_' to avoid breaking the access log (inconsistent number
  // of segments between log entries when the separator is whitespace).
  std::string sanitized = StringUtil::replaceAllEmptySpace(policy_id);
  return fmt::format("rbac_access_denied_matched_policy[{}]", sanitized);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
