#include "source/extensions/filters/common/rbac/utility.h"

#include <string>

#include "source/common/config/well_known_names.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

namespace {

// Joins the non-empty pieces with dots.
std::string dotJoin(std::initializer_list<absl::string_view> pieces) {
  absl::InlinedVector<absl::string_view, 4> non_empty;
  for (const absl::string_view piece : pieces) {
    if (!piece.empty()) {
      non_empty.push_back(piece);
    }
  }
  return absl::StrJoin(non_empty, ".");
}

// Builds the tagged prefix "<prefix>.rbac.<group_prefix>[.policy]" of one stats group (enforced
// or shadow). The flat name is identical to the legacy one.
Stats::TaggedStatName makeTaggedPrefix(absl::string_view prefix, absl::string_view group_prefix,
                                       bool http_filter, bool per_policy,
                                       Stats::SymbolTable& symbol_table) {
  // Tolerate trailing dots in the configured prefixes.
  prefix = absl::StripSuffix(prefix, ".");
  group_prefix = absl::StripSuffix(group_prefix, ".");

  const std::string flat = dotJoin({prefix, "rbac", group_prefix, per_policy ? "policy" : ""});
  const absl::string_view base = per_policy ? "rbac.policy" : "rbac";

  // A non-empty prefix is the network filter's own stat_prefix and becomes a tag; the HTTP
  // filter's prefix is empty (its parent's prefix lives in the prefixed scope).
  absl::InlinedVector<Stats::TagStringView, 2> tags;
  if (!prefix.empty()) {
    tags.push_back({Config::TagNames::get().RBAC_PREFIX, prefix});
  }
  if (!group_prefix.empty()) {
    // TODO(wbpcode): consider to unify the tag name for HTTP and network filters.
    const absl::string_view tag_name = http_filter ? Config::TagNames::get().RBAC_HTTP_PREFIX
                                                   : Config::TagNames::get().RBAC_RULES_PREFIX;
    tags.push_back({tag_name, group_prefix});
  }
  return {symbol_table, base, tags, flat};
}

} // namespace

PerPolicyStatNames::PerPolicyStatNames(Stats::SymbolTable& symbol_table)
    : pool_(symbol_table), policy_tag_(pool_.add(Config::TagNames::get().RBAC_POLICY_NAME)),
      allowed_(pool_.add("allowed")), denied_(pool_.add("denied")),
      shadow_allowed_(pool_.add("shadow_allowed")), shadow_denied_(pool_.add("shadow_denied")) {}

RoleBasedAccessControlFilterStats generateStats(const std::string& prefix,
                                                const std::string& rules_prefix,
                                                const std::string& shadow_rules_prefix,
                                                bool http_filter, Stats::Scope& scope) {
  Stats::SymbolTable& symbol_table = scope.symbolTable();
  const auto rules = makeTaggedPrefix(prefix, rules_prefix, http_filter, false, symbol_table);
  const auto shadow =
      makeTaggedPrefix(prefix, shadow_rules_prefix, http_filter, false, symbol_table);
  Stats::LiteScopeHelper rules_helper(scope, rules.baseName(), rules.tags(), rules.name());
  Stats::LiteScopeHelper shadow_helper(scope, shadow.baseName(), shadow.tags(), shadow.name());
  return {
      ENFORCE_RBAC_FILTER_STATS(POOL_COUNTER(rules_helper))
          SHADOW_RBAC_FILTER_STATS(POOL_COUNTER(shadow_helper)) scope,
      makeTaggedPrefix(prefix, rules_prefix, http_filter, true, symbol_table),
      makeTaggedPrefix(prefix, shadow_rules_prefix, http_filter, true, symbol_table),
      PerPolicyStatNames(symbol_table),
  };
}

void RoleBasedAccessControlFilterStats::incCounter(const Stats::TaggedStatName& prefix,
                                                   absl::string_view policy, Stats::StatName leaf) {
  Stats::SymbolTable& symbol_table = scope_.symbolTable();
  const Stats::StatNameDynamicStorage policy_name(policy, symbol_table);

  // Flat name "<prefix>.<policy>.<leaf>", tag-extracted name "<base>.<leaf>", policy name tag.
  const Stats::StatNameJoiner flat_name({prefix.name(), policy_name.statName(), leaf},
                                        symbol_table);
  const Stats::StatNameJoiner base_name({prefix.baseName(), leaf}, symbol_table);

  Stats::StatNameTagVec tags(prefix.tags().begin(), prefix.tags().end());
  tags.emplace_back(names_.policy_tag_, policy_name.statName());
  scope_.counterFromTaggedName(base_name.statName(), tags, flat_name.statName()).inc();
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
