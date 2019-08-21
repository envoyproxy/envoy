#include "extensions/filters/network/common/redis/redis_command_stats.h"

#include "extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

Stats::SymbolTable::StoragePtr RedisCommandStats::addPrefix(const Stats::StatName name) {
  return scope_.symbolTable().join({prefix_, name});
}

Stats::Counter& RedisCommandStats::counter(std::string name) {
  Stats::StatName stat_name = stat_name_set_.getStatName(name);
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(stat_name);
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::Histogram& RedisCommandStats::histogram(std::string name) {
  Stats::StatName stat_name = stat_name_set_.getStatName(name);
  return histogram(stat_name);
}

Stats::Histogram& RedisCommandStats::histogram(Stats::StatName stat_name) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(stat_name);
  return scope_.histogramFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::CompletableTimespanPtr
RedisCommandStats::createCommandTimer(std::string name, Envoy::TimeSource& time_source) {
  Stats::StatName stat_name = stat_name_set_.getStatName(name + latency_suffix_);
  return std::make_unique<Stats::TimespanWithUnit<std::chrono::microseconds>>(histogram(stat_name),
                                                                              time_source);
}

Stats::CompletableTimespanPtr
RedisCommandStats::createAggregateTimer(Envoy::TimeSource& time_source) {
  return std::make_unique<Stats::TimespanWithUnit<std::chrono::microseconds>>(
      histogram(upstream_rq_time_), time_source);
}

std::string RedisCommandStats::getCommandFromRequest(const RespValue& request) {
  // Get command from RespValue
  switch (request.type()) {
  case RespType::Array:
    return getCommandFromRequest(request.asArray().front());
  case RespType::Integer:
    return unknown_metric_;
  case RespType::Null:
    return null_metric_;
  default:
    return request.asString();
  }
}

void RedisCommandStats::updateStatsTotal(std::string command) {
  counter(command + total_suffix_).inc();
}

void RedisCommandStats::updateStats(const bool success, std::string command) {
  if (success) {
    counter(command + success_suffix_).inc();
  } else {
    counter(command + error_suffix_).inc();
  }
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy