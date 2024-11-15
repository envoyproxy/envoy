#include "contrib/dynamo/filters/http/source/dynamo_stats.h"

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "source/common/stats/symbol_table.h"

#include "contrib/dynamo/filters/http/source/dynamo_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

DynamoStats::DynamoStats(Stats::Scope& scope, const std::string& prefix)
    : scope_(scope), stat_name_set_(scope.symbolTable().makeSet("Dynamo")),
      prefix_(stat_name_set_->add(prefix + "dynamodb")),
      batch_failure_unprocessed_keys_(stat_name_set_->add("BatchFailureUnprocessedKeys")),
      capacity_(stat_name_set_->add("capacity")),
      empty_response_body_(stat_name_set_->add("empty_response_body")),
      error_(stat_name_set_->add("error")),
      invalid_req_body_(stat_name_set_->add("invalid_req_body")),
      invalid_resp_body_(stat_name_set_->add("invalid_resp_body")),
      multiple_tables_(stat_name_set_->add("multiple_tables")),
      no_table_(stat_name_set_->add("no_table")),
      operation_missing_(stat_name_set_->add("operation_missing")),
      table_(stat_name_set_->add("table")), table_missing_(stat_name_set_->add("table_missing")),
      upstream_rq_time_(stat_name_set_->add("upstream_rq_time")),
      upstream_rq_total_(stat_name_set_->add("upstream_rq_total")),
      unknown_entity_type_(stat_name_set_->add("unknown_entity_type")),
      unknown_operation_(stat_name_set_->add("unknown_operation")) {
  upstream_rq_total_groups_[0] = stat_name_set_->add("upstream_rq_total_unknown");
  upstream_rq_time_groups_[0] = stat_name_set_->add("upstream_rq_time_unknown");
  for (size_t i = 1; i < DynamoStats::NumGroupEntries; ++i) {
    upstream_rq_total_groups_[i] = stat_name_set_->add(fmt::format("upstream_rq_total_{}xx", i));
    upstream_rq_time_groups_[i] = stat_name_set_->add(fmt::format("upstream_rq_time_{}xx", i));
  }
  RequestParser::forEachStatString(
      [this](const std::string& str) { stat_name_set_->rememberBuiltin(str); });
  for (uint32_t status_code : {200, 400, 403, 502}) {
    stat_name_set_->rememberBuiltin(absl::StrCat("upstream_rq_time_", status_code));
    stat_name_set_->rememberBuiltin(absl::StrCat("upstream_rq_total_", status_code));
  }
  stat_name_set_->rememberBuiltins({"operation", "table"});
}

Stats::ElementVec DynamoStats::addPrefix(const Stats::ElementVec& names) {
  Stats::ElementVec names_with_prefix;
  names_with_prefix.reserve(1 + names.size());
  names_with_prefix.push_back(prefix_);
  names_with_prefix.insert(names_with_prefix.end(), names.begin(), names.end());
  return names_with_prefix;
}

void DynamoStats::incCounter(const Stats::ElementVec& names) {
  Stats::Utility::counterFromElements(scope_, addPrefix(names)).inc();
}

void DynamoStats::recordHistogram(const Stats::ElementVec& names, Stats::Histogram::Unit unit,
                                  uint64_t value) {
  Stats::Utility::histogramFromElements(scope_, addPrefix(names), unit).recordValue(value);
}

Stats::Counter& DynamoStats::buildPartitionStatCounter(const std::string& table_name,
                                                       const std::string& operation,
                                                       const std::string& partition_id) {
  // Use the last 7 characters of the partition id.
  absl::string_view id_last_7 = absl::string_view(partition_id).substr(partition_id.size() - 7);
  std::string partition = absl::StrCat("__partition_id=", id_last_7);
  return Stats::Utility::counterFromElements(
      scope_,
      addPrefix({table_, Stats::DynamicName(table_name), capacity_,
                 getBuiltin(operation, unknown_operation_), Stats::DynamicName(partition)}));
}

size_t DynamoStats::groupIndex(uint64_t status) {
  size_t index = status / 100;
  if (index >= NumGroupEntries) {
    index = 0; // status-code 600 or higher is unknown.
  }
  return index;
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
