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
  for (int i = 1; i < 6; ++i) {
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

  // TODO(jmarantz): should we also learn some 'time' and 'total' stat-names for 200, 404, 503 etc?
}

Stats::SymbolTable::StoragePtr DynamoStats::addPrefix(const std::vector<Stats::StatName>& names) {
  std::vector<Stats::StatName> names_with_prefix{prefix_};
  names_with_prefix.insert(names_with_prefix.end(), names.begin(), names.end());
  return symbolTable().join(names_with_prefix);
}

Stats::Counter& DynamoStats::counter(const std::vector<Stats::StatName>& names) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(names);
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::Histogram& DynamoStats::histogram(const std::vector<Stats::StatName>& names) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(names);
  return scope_.histogramFromStatName(Stats::StatName(stat_name_storage.get()));
}

/**
 * Creates the partition id stats string.
 * The stats format is
 * "<stat_prefix>table.<table_name>.capacity.<operation>.__partition_id=<partition_id>".
 * Partition ids and dynamodb table names can be long. To satisfy the string length,
 * we truncate in two ways:
 * 1. We only take the last 7 characters of the partition id.
 * 2. If the stats string with <table_name> is longer than the stats MAX_NAME_SIZE, we will
 * truncate the table name to
 * fit the size requirements.
 */
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

Stats::StatName DynamoStats::getStatName(const std::string& str) {
  // We have been presented with a string to when composing a stat. The Dynamo
  // system has a few well-known names that we have saved during construction,
  // and we can access StatNames for those without a lock.
  auto iter = builtin_stat_names_.find(str);
  if (iter != builtin_stat_names_.end()) {
    return iter->second;
  }

  // However, some of the names come from json data in requests, so we need
  // to learn these, and we'll need to take a lock in this structure to see
  // if we already have allocated a StatName for such names. If we haven't,
  // then we'll have to take the more-likely-contented symbol-table lock to
  // allocate one, which we'll then remember in dynamic_stat_names_.
  absl::MutexLock lock(&mutex_);
  Stats::StatName& stat_name = dynamic_stat_names_[str];
  if (stat_name.empty()) { // Note that builtin_stat_names_ already has one for "".
    stat_name = pool_.add(str);
  }
  return stat_name;
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
