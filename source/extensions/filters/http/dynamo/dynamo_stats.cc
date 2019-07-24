#include "extensions/filters/http/dynamo/dynamo_stats.h"

#include <iostream>
#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/http/dynamo/dynamo_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

DynamoStats::DynamoStats(Stats::Scope& scope, const std::string& prefix)
    : scope_(scope), pool_(scope.symbolTable()), prefix_(pool_.add(prefix + "dynamodb")),
      batch_failure_unprocessed_keys_(pool_.add("BatchFailureUnprocessedKeys")),
      capacity_(pool_.add("capacity")), empty_response_body_(pool_.add("empty_response_body")),
      error_(pool_.add("error")), invalid_req_body_(pool_.add("invalid_req_body")),
      invalid_resp_body_(pool_.add("invalid_resp_body")),
      multiple_tables_(pool_.add("multiple_tables")), no_table_(pool_.add("no_table")),
      operation_missing_(pool_.add("operation_missing")), table_(pool_.add("table")),
      table_missing_(pool_.add("table_missing")), upstream_rq_time_(pool_.add("upstream_rq_time")),
      upstream_rq_total_(pool_.add("upstream_rq_total")) {
  upstream_rq_total_groups_[0] = pool_.add("upstream_rq_total_unknown");
  upstream_rq_time_groups_[0] = pool_.add("upstream_rq_time_unknown");
  for (size_t i = 1; i < DynamoStats::NumGroupEntries; ++i) {
    upstream_rq_total_groups_[i] = pool_.add(fmt::format("upstream_rq_total_{}xx", i));
    upstream_rq_time_groups_[i] = pool_.add(fmt::format("upstream_rq_time_{}xx", i));
  }
  RequestParser::forEachStatString([this](const std::string& str) {
    // Thread annotation does not realize this function is only called from the
    // constructor, so we need to lock the mutex. It's easier to just do that
    // and it's no real penalty.
    absl::MutexLock lock(&mutex_);
    builtin_stat_names_[str] = pool_.add(str);
  });
  builtin_stat_names_[""] = Stats::StatName();
}

Stats::SymbolTable::StoragePtr DynamoStats::addPrefix(const std::vector<Stats::StatName>& names) {
  std::vector<Stats::StatName> names_with_prefix{prefix_};
  names_with_prefix.reserve(names.end() - names.begin());
  names_with_prefix.insert(names_with_prefix.end(), names.begin(), names.end());
  return scope_.symbolTable().join(names_with_prefix);
}

Stats::Counter& DynamoStats::counter(const std::vector<Stats::StatName>& names) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(names);
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::Histogram& DynamoStats::histogram(const std::vector<Stats::StatName>& names) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(names);
  return scope_.histogramFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::Counter& DynamoStats::buildPartitionStatCounter(const std::string& table_name,
                                                       const std::string& operation,
                                                       const std::string& partition_id) {
  // Use the last 7 characters of the partition id.
  absl::string_view id_last_7 = absl::string_view(partition_id).substr(partition_id.size() - 7);
  const Stats::SymbolTable::StoragePtr stat_name_storage =
      addPrefix({table_, getStatName(table_name), capacity_, getStatName(operation),
                 getStatName(absl::StrCat("__partition_id=", id_last_7))});
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}

size_t DynamoStats::groupIndex(uint64_t status) {
  size_t index = status / 100;
  if (index >= NumGroupEntries) {
    index = 0; // status-code 600 or higher is unknown.
  }
  return index;
}

Stats::StatName DynamoStats::getStatName(const std::string& token) {
  // The Dynamo system has a few well-known tokens that we have stored during
  // construction, and we can access StatNames for those without a lock. Note
  // that we only mutate builtin_stat_names_ during construction.
  auto iter = builtin_stat_names_.find(token);
  if (iter != builtin_stat_names_.end()) {
    return iter->second;
  }

  // However, some of the tokens come from json data received during requests,
  // so we need to store these in a mutex-protected map. Once we hold the mutex,
  // we can check dynamic_stat_names_. If it's missing we'll have to add it
  // to the symbol table, whose mutex is more likely to be contended, and then
  // store it in dynamic_stat_names.
  //
  // TODO(jmarantz): Potential perf issue here with contention, both on this
  // mutex and also the SymbolTable mutex which must be taken during
  // StatNamePool::add().
  absl::MutexLock lock(&mutex_);
  Stats::StatName& stat_name = dynamic_stat_names_[token];
  if (stat_name.empty()) { // Note that builtin_stat_names_ already has one for "".
    stat_name = pool_.add(token);
  }
  return stat_name;
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
