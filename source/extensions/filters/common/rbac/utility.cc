#include "extensions/filters/common/rbac/utility.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

RoleBasedAccessControlFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "rbac.";
  return {ALL_RBAC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

std::string responseDetail(const std::string& policy_id) {
  // Replace whitespaces in policy_id with '_' to avoid potential breaks in the access log.
  std::string sanitized =
      StringUtil::replaceCharacters(policy_id, StringUtil::WhitespaceChars, '_');
  return fmt::format("rbac_access_denied_matched_policy[{}]", sanitized);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
