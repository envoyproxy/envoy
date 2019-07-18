#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"
#include "extensions/filters/http/dynamo/dynamo_stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

DynamoStats::DynamoStats(Stats::Scope& scope, const std::string& prefix)
    : scope_(scope),
      pool_(scope.symbolTable()),
      prefix_(pool_.add(prefix + "dynamodb")),
      batch_failure_unprocessed_keys_(pool_.add("BatchFailureUnprocessedKeys")),
      capacity_(pool_.add("capacity")),
      empty_response_body_(pool_.add("empty_response_body")),
      error_(pool_.add("error")),
      invalid_req_body_(pool_.add("invalid_req_body")),
      invalid_resp_body_(pool_.add("invalid_resp_body")),
      multiple_tables_(pool_.add("multiple_tables")),
      no_table_(pool_.add("no_table")),
      operation_missing_(pool_.add("operation_missing")),
      table_(pool_.add("table")),
      table_missing_(pool_.add("table_missing")),
      upstream_rq_time_(pool_.add("upstream_rq_time")),
      upstream_rq_total_(pool_.add("upstream_rq_total")) {
  upstream_rq_total_groups_[0] = pool_.add("upstream_rq_total_unknown");
  upstream_rq_time_groups_[0] = pool_.add("upstream_rq_time_unknown");
  for (int i = 1; i < 6; ++i) {
    upstream_rq_total_groups_[i] = pool_.add(fmt::format("upstream_rq_total_{}xx", i));
    upstream_rq_time_groups_[i] = pool_.add(fmt::format("upstream_rq_time_{}xx", i));
  }
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
Stats::Counter& DynamoStats::buildPartitionStatCounter(
    const std::string& table_name, const std::string& operation, const std::string& partition_id) {
  // Use the last 7 characters of the partition id.
  absl::string_view id_last_7 = absl::string_view(partition_id).substr(partition_id.size() - 7);
  Stats::StatNamePool pool(symbolTable());
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix({
      table_, pool_.add(table_name), capacity_, pool.add(operation),
      pool.add(absl::StrCat("__partition_id=", id_last_7))
    });
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}


} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
