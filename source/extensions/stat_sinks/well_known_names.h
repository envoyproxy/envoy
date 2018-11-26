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
  const std::string Statsd = "envoy.statsd";
  // DogStatsD compatible stastsd sink
  const std::string DogStatsd = "envoy.dog_statsd";
  // MetricsService sink
  const std::string MetricsService = "envoy.metrics_service";
  // Hystrix sink
  const std::string Hystrix = "envoy.stat_sinks.hystrix";
};

typedef ConstSingleton<StatsSinkNameValues> StatsSinkNames;

} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
