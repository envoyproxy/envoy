#include "extensions/filters/network/mongo_proxy/mongo_stats.h"

#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

MongoStats::MongoStats(Stats::Scope& scope, absl::string_view prefix,
                       const std::vector<std::string>& commands)
    : scope_(scope), stat_name_set_(scope.symbolTable().makeSet("Mongo")),
      prefix_(stat_name_set_->add(prefix)), callsite_(stat_name_set_->add("callsite")),
      cmd_(stat_name_set_->add("cmd")), collection_(stat_name_set_->add("collection")),
      multi_get_(stat_name_set_->add("multi_get")),
      reply_num_docs_(stat_name_set_->add("reply_num_docs")),
      reply_size_(stat_name_set_->add("reply_size")),
      reply_time_ms_(stat_name_set_->add("reply_time_ms")),
      time_ms_(stat_name_set_->add("time_ms")), query_(stat_name_set_->add("query")),
      scatter_get_(stat_name_set_->add("scatter_get")), total_(stat_name_set_->add("total")),
      unknown_command_(stat_name_set_->add("unknown_command")) {

  for (const auto& cmd : commands) {
    stat_name_set_->rememberBuiltin(cmd);
  }
}

Stats::ElementVec MongoStats::addPrefix(const Stats::ElementVec& names) {
  Stats::ElementVec names_with_prefix;
  names_with_prefix.reserve(1 + names.size());
  names_with_prefix.push_back(prefix_);
  names_with_prefix.insert(names_with_prefix.end(), names.begin(), names.end());
  return names_with_prefix;
}

void MongoStats::incCounter(const Stats::ElementVec& names) {
  Stats::Utility::counterFromElements(scope_, addPrefix(names)).inc();
}

void MongoStats::recordHistogram(const Stats::ElementVec& names, Stats::Histogram::Unit unit,
                                 uint64_t sample) {
  Stats::Utility::histogramFromElements(scope_, addPrefix(names), unit).recordValue(sample);
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
