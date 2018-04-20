#pragma once

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
  const std::string STATSD = "envoy.statsd";
  // DogStatsD compatible stastsd sink
  const std::string DOG_STATSD = "envoy.dog_statsd";
  // MetricsService sink
  const std::string METRICS_SERVICE = "envoy.metrics_service";
};

typedef ConstSingleton<StatsSinkNameValues> StatsSinkNames;

} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
