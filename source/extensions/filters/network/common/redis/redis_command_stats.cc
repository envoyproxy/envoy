#include "source/extensions/filters/network/common/redis/redis_command_stats.h"

#include "source/common/stats/timespan_impl.h"
#include "source/common/stats/utility.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

RedisCommandStats::RedisCommandStats(Stats::SymbolTable& symbol_table, const std::string& prefix)
    : symbol_table_(symbol_table), stat_name_set_(symbol_table_.makeSet("Redis")),
      prefix_(stat_name_set_->add(prefix)),
      upstream_rq_time_(stat_name_set_->add("upstream_rq_time")),
      latency_(stat_name_set_->add("latency")), total_(stat_name_set_->add("total")),
      success_(stat_name_set_->add("success")), failure_(stat_name_set_->add("failure")),
      unused_metric_(stat_name_set_->add("unused")), null_metric_(stat_name_set_->add("null")),
      unknown_metric_(stat_name_set_->add("unknown")) {
  // Note: Even if this is disabled, we track the upstream_rq_time.
  // Create StatName for each Redis command. Note that we don't include Auth or Ping.
  stat_name_set_->rememberBuiltins(
      Extensions::NetworkFilters::Common::Redis::SupportedCommands::simpleCommands());
  stat_name_set_->rememberBuiltins(
      Extensions::NetworkFilters::Common::Redis::SupportedCommands::evalCommands());
  stat_name_set_->rememberBuiltins(Extensions::NetworkFilters::Common::Redis::SupportedCommands::
                                       hashMultipleSumResultCommands());
  stat_name_set_->rememberBuiltin(
      Extensions::NetworkFilters::Common::Redis::SupportedCommands::mget());
  stat_name_set_->rememberBuiltin(
      Extensions::NetworkFilters::Common::Redis::SupportedCommands::mset());
}

Stats::TimespanPtr RedisCommandStats::createCommandTimer(Stats::Scope& scope,
                                                         Stats::StatName command,
                                                         Envoy::TimeSource& time_source) {
  return std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      Stats::Utility::histogramFromStatNames(scope, {prefix_, command, latency_},
                                             Stats::Histogram::Unit::Microseconds),
      time_source);
}

Stats::TimespanPtr RedisCommandStats::createAggregateTimer(Stats::Scope& scope,
                                                           Envoy::TimeSource& time_source) {
  return std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      Stats::Utility::histogramFromStatNames(scope, {prefix_, upstream_rq_time_},
                                             Stats::Histogram::Unit::Microseconds),
      time_source);
}

Stats::StatName RedisCommandStats::getCommandFromRequest(const RespValue& request) {
  // Get command from RespValue
  switch (request.type()) {
  case RespType::Array:
    return getCommandFromRequest(request.asArray().front());
  case RespType::CompositeArray:
    return getCommandFromRequest(*request.asCompositeArray().command());
  case RespType::Null:
    return null_metric_;
  case RespType::BulkString:
  case RespType::SimpleString: {
    std::string to_lower_command = absl::AsciiStrToLower(request.asString());
    return stat_name_set_->getBuiltin(to_lower_command, unknown_metric_);
  }
  case RespType::Integer:
  case RespType::Error:
  default:
    return unknown_metric_;
  }
}

void RedisCommandStats::updateStatsTotal(Stats::Scope& scope, Stats::StatName command) {
  Stats::Utility::counterFromStatNames(scope, {prefix_, command, total_}).inc();
}

void RedisCommandStats::updateStats(Stats::Scope& scope, Stats::StatName command,
                                    const bool success) {
  Stats::StatName status = success ? success_ : failure_;
  Stats::Utility::counterFromStatNames(scope, {prefix_, command, status}).inc();
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
