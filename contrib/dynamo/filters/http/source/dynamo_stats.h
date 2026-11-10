#pragma once

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

class DynamoStats {
public:
  DynamoStats(Stats::Scope& scope, const std::string& prefix);

  /**
   * Increments '<prefix>dynamodb.<names>'. The stat carries no tags of its own.
   */
  void incCounter(absl::Span<const Stats::StatName> names);

  /**
   * Increments '<prefix>dynamodb.<entity_type>.<entity>.<name>', where the entity (a table or an
   * operation) is emitted as the `entity_tag` tag and dropped from the tag-extracted name.
   */
  void incEntityCounter(Stats::StatName entity_type, Stats::StatName entity_tag,
                        Stats::StatName entity, Stats::StatName name);

  /**
   * Records `value` in the '<prefix>dynamodb.<entity_type>.<entity>.<name>' histogram, tagged like
   * incEntityCounter().
   */
  void recordEntityHistogram(Stats::StatName entity_type, Stats::StatName entity_tag,
                             Stats::StatName entity, Stats::StatName name,
                             Stats::Histogram::Unit unit, uint64_t value);

  /**
   * Increments '<prefix>dynamodb.error.<table>.<name>', with the table as a tag.
   */
  void incTableErrorCounter(Stats::StatName table, Stats::StatName name);

  /**
   * Creates the partition id stats string. The stats format is
   * "<stat_prefix>table.<table_name>.capacity.<operation>.__partition_id=<partition_id>".
   * Partition ids and dynamodb table names can be long. To satisfy the string
   * length, we truncate, taking only the last 7 characters of the partition id.
   * The table, the operation and the (truncated) partition id are tags of the stat.
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

  Stats::SymbolTable& symbolTable() { return scope_->symbolTable(); }

private:
  /**
   * Creates '<prefix>dynamodb.<names>' whose tag-extracted name is '<prefix>dynamodb.<base_names>'
   * and which carries `tags`.
   */
  Stats::Counter& counter(absl::Span<const Stats::StatName> base_names, Stats::StatNameTagSpan tags,
                          absl::Span<const Stats::StatName> names);
  Stats::Histogram& histogram(absl::Span<const Stats::StatName> base_names,
                              Stats::StatNameTagSpan tags, absl::Span<const Stats::StatName> names,
                              Stats::Histogram::Unit unit);

  // The '<prefix>dynamodb' subscope all the stats live in: its name prefixes them.
  const Stats::ScopeSharedPtr scope_;
  Stats::StatNameSetPtr stat_name_set_;

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
  // The tags the operations, tables and partition ids are emitted as.
  const Stats::StatName operation_tag_;
  const Stats::StatName table_tag_;
  const Stats::StatName partition_id_tag_;

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
