#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {

/**
 * Well-known stats sink names.
 * NOTE: New sinks should use the well known name: envoy.stat_sinks.name.
 */
class StatsSinkNameValues {
public:
  // Statsd sink
  const std::string Statsd = "envoy.stat_sinks.statsd";
  // DogStatsD compatible statsd sink
  const std::string DogStatsd = "envoy.stat_sinks.dog_statsd";
  // MetricsService sink
  const std::string MetricsService = "envoy.stat_sinks.metrics_service";
  // Hystrix sink
  const std::string Hystrix = "envoy.stat_sinks.hystrix";
};

using StatsSinkNames = ConstSingleton<StatsSinkNameValues>;

} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
