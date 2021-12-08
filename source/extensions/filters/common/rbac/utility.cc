#include "source/extensions/filters/common/rbac/utility.h"

#include <string>

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlFilterStats
generateStats(const std::string& prefix, const std::string& shadow_prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "rbac.";
  return {ENFORCE_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))
              SHADOW_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix + shadow_prefix))};
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
