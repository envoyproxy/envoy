#pragma once

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

class DynamoStats {
public:
  DynamoStats(Stats::Scope& scope, const std::string& prefix);

  void incCounter(const Stats::StatNameVec& names);
  void recordHistogram(const Stats::StatNameVec& names, Stats::Histogram::Unit unit,
                       uint64_t value);

  /**
   * Creates the partition id stats string. The stats format is
   * "<stat_prefix>table.<table_name>.capacity.<operation>.__partition_id=<partition_id>".
   * Partition ids and dynamodb table names can be long. To satisfy the string
   * length, we truncate, taking only the last 7 characters of the partition id.
   */
  Stats::Counter& buildPartitionStatCounter(const std::string& table_name,
                                            const std::string& operation,
                                            const std::string& partition_id);

  static size_t groupIndex(uint64_t status);

  /**
   * Finds a StatName by string.
   */
  Stats::StatName getBuiltin(const std::string& str, Stats::StatName fallback) {
    return stat_name_set_->getBuiltin(str, fallback);
  }

  Stats::SymbolTable& symbolTable() { return scope_.symbolTable(); }

private:
  Stats::SymbolTable::StoragePtr addPrefix(const Stats::StatNameVec& names);

  Stats::Scope& scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName prefix_;

public:
  const Stats::StatName batch_failure_unprocessed_keys_;
  const Stats::StatName capacity_;
  const Stats::StatName empty_response_body_;
  const Stats::StatName error_;
  const Stats::StatName invalid_req_body_;
  const Stats::StatName invalid_resp_body_;
  const Stats::StatName multiple_tables_;
  const Stats::StatName no_table_;
  const Stats::StatName operation_missing_;
  const Stats::StatName table_;
  const Stats::StatName table_missing_;
  const Stats::StatName upstream_rq_time_;
  const Stats::StatName upstream_rq_total_;
  const Stats::StatName upstream_rq_unknown_;
  const Stats::StatName unknown_entity_type_;
  const Stats::StatName unknown_operation_;

  // Keep group codes for HTTP status codes through the 500s.
  static constexpr size_t NumGroupEntries = 6;
  Stats::StatName upstream_rq_total_groups_[NumGroupEntries];
  Stats::StatName upstream_rq_time_groups_[NumGroupEntries];
};
using DynamoStatsSharedPtr = std::shared_ptr<DynamoStats>;

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
