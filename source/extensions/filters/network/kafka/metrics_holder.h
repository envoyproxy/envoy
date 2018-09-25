#pragma once

#include "extensions/filters/network/kafka/kafka_types.h"

#include "envoy/stats/scope.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

struct RequestMetrics {
  Stats::Counter& counter;
  Stats::Histogram& duration;
};

class MetricsHolder {
public:
  MetricsHolder(Stats::Scope& scope);
  RequestMetrics& metric(INT16 api_key_);

private:
  std::vector<RequestMetrics> metrics_;
  RequestMetrics other_handler_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
