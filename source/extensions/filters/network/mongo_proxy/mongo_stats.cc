#include "source/extensions/filters/network/mongo_proxy/mongo_stats.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/well_known_stat_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

namespace {

// Creates the `mongo.<prefix>` scope with the prefix attached as an explicit tag. The names are
// encoded as StatNames directly because the string-based scope API sanitizes the names and would
// rewrite a prefix containing e.g. ':' even when explicit tags are disabled.
Stats::ScopeSharedPtr createStatsScope(Stats::Scope& scope, absl::string_view prefix) {
  const Stats::StatNameManagedStorage tag_value(prefix, scope.symbolTable());

  const Stats::WellKnownStatNames& names = scope.symbolTable().wellKnownStatNames();
  const Stats::StatNameTag tag{names.tag_mongo_prefix_, tag_value.statName()};
  const Stats::StatNameJoiner tagged({names.mongo_, tag_value.statName()}, scope.symbolTable());
  return scope.scopeFromTaggedName(names.mongo_, Stats::StatNameTagSpan{tag}, tagged.statName());
}

} // namespace

MongoStats::MongoStats(Stats::Scope& scope, absl::string_view prefix,
                       const std::vector<std::string>& commands)
    : scope_(createStatsScope(scope, prefix)),
      stat_name_set_(scope_->symbolTable().makeSet("Mongo")),
      callsite_(stat_name_set_->add("callsite")), cmd_(stat_name_set_->add("cmd")),
      collection_(stat_name_set_->add("collection")), multi_get_(stat_name_set_->add("multi_get")),
      reply_num_docs_(stat_name_set_->add("reply_num_docs")),
      reply_size_(stat_name_set_->add("reply_size")),
      reply_time_ms_(stat_name_set_->add("reply_time_ms")),
      time_ms_(stat_name_set_->add("time_ms")), query_(stat_name_set_->add("query")),
      scatter_get_(stat_name_set_->add("scatter_get")), total_(stat_name_set_->add("total")),
      unknown_command_(stat_name_set_->add("unknown_command")),
      callsite_tag_(stat_name_set_->add(Envoy::Config::TagNames::get().MONGO_CALLSITE)),
      cmd_tag_(stat_name_set_->add(Envoy::Config::TagNames::get().MONGO_CMD)),
      collection_tag_(stat_name_set_->add(Envoy::Config::TagNames::get().MONGO_COLLECTION)),
      proxy_stats_{ALL_MONGO_PROXY_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_),
                                         POOL_HISTOGRAM(*scope_))} {

  for (const auto& cmd : commands) {
    stat_name_set_->rememberBuiltin(cmd);
  }
}

void MongoStats::incCounter(Stats::StatNameSpan base_names, Stats::StatNameTagSpan tags,
                            Stats::StatNameSpan names) {
  Stats::Utility::counterFromTaggedStatNames(*scope_, base_names, tags, names).inc();
}

void MongoStats::recordHistogram(Stats::StatNameSpan base_names, Stats::StatNameTagSpan tags,
                                 Stats::StatNameSpan names, Stats::Histogram::Unit unit,
                                 uint64_t sample) {
  Stats::Utility::histogramFromTaggedStatNames(*scope_, base_names, tags, names, unit)
      .recordValue(sample);
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
