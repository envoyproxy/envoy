#include "extensions/filters/network/common/redis/redis_command_stats.h"

#include "extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

RedisCommandStats::RedisCommandStats(Stats::Scope& scope, const std::string& prefix, bool enabled)
    : scope_(scope), stat_name_pool_(scope.symbolTable()), prefix_(stat_name_pool_.add(prefix)),
      enabled_(enabled), upstream_rq_time_(stat_name_pool_.add("upstream_rq_time")),
      latency_(stat_name_pool_.add("latency")), total_(stat_name_pool_.add("total")),
      success_(stat_name_pool_.add("success")), error_(stat_name_pool_.add("error")),
      unused_metric_(stat_name_pool_.add("unused")), null_metric_(stat_name_pool_.add("null")),
      unknown_metric_(stat_name_pool_.add("unknown")) {
  // Note: Even if this is disabled, we track the upstream_rq_time.
  if (enabled_) {
    // Create StatName for each Redis command. Note that we don't include Auth or Ping.
    for (const std::string& command :
         Extensions::NetworkFilters::Common::Redis::SupportedCommands::simpleCommands()) {
      addCommandToPool(command);
    }
    for (const std::string& command :
         Extensions::NetworkFilters::Common::Redis::SupportedCommands::evalCommands()) {
      addCommandToPool(command);
    }
    for (const std::string& command : Extensions::NetworkFilters::Common::Redis::SupportedCommands::
             hashMultipleSumResultCommands()) {
      addCommandToPool(command);
    }
    addCommandToPool(Extensions::NetworkFilters::Common::Redis::SupportedCommands::mget());
    addCommandToPool(Extensions::NetworkFilters::Common::Redis::SupportedCommands::mset());
  }
}

void RedisCommandStats::addCommandToPool(const std::string& command_string) {
  Stats::StatName command = stat_name_pool_.add(command_string);
  stat_name_map_[command_string] = command;
}

Stats::Counter& RedisCommandStats::counter(const Stats::StatNameVec& stat_names) {
  const Stats::SymbolTable::StoragePtr storage_ptr = scope_.symbolTable().join(stat_names);
  Stats::StatName full_stat_name = Stats::StatName(storage_ptr.get());
  return scope_.counterFromStatName(full_stat_name);
}

Stats::Histogram& RedisCommandStats::histogram(const Stats::StatNameVec& stat_names) {
  const Stats::SymbolTable::StoragePtr storage_ptr = scope_.symbolTable().join(stat_names);
  Stats::StatName full_stat_name = Stats::StatName(storage_ptr.get());
  return scope_.histogramFromStatName(full_stat_name);
}

Stats::CompletableTimespanPtr
RedisCommandStats::createCommandTimer(Stats::StatName command, Envoy::TimeSource& time_source) {
  return std::make_unique<Stats::TimespanWithUnit<std::chrono::microseconds>>(
      histogram({prefix_, command, latency_}), time_source);
}

Stats::CompletableTimespanPtr
RedisCommandStats::createAggregateTimer(Envoy::TimeSource& time_source) {
  return std::make_unique<Stats::TimespanWithUnit<std::chrono::microseconds>>(
      histogram({prefix_, upstream_rq_time_}), time_source);
}

Stats::StatName RedisCommandStats::getCommandFromRequest(const RespValue& request) {
  // Get command from RespValue
  switch (request.type()) {
  case RespType::Array:
    return getCommandFromRequest(request.asArray().front());
  case RespType::Integer:
    return unknown_metric_;
  case RespType::Null:
    return null_metric_;
  default:
    // Once we have a RespType::String we lowercase it and then look it up in our stat_name_map.
    // If it does not exist, we return our unknown stat name.
    std::string to_lower_command(request.asString());
    to_lower_table_.toLowerCase(to_lower_command);

    auto iter = stat_name_map_.find(to_lower_command);
    if (iter != stat_name_map_.end()) {
      return iter->second;
    } else {
      return unknown_metric_;
    }
  }
}

void RedisCommandStats::updateStatsTotal(Stats::StatName command) {
  counter({prefix_, command, total_}).inc();
}

void RedisCommandStats::updateStats(const bool success, Stats::StatName command) {
  if (success) {
    counter({prefix_, command, success_}).inc();
  } else {
    counter({prefix_, command, success_}).inc();
  }
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy