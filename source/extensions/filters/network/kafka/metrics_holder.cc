#include "extensions/filters/network/kafka/metrics_holder.h"

#include "common/common/to_lower_table.h"

#include "extensions/filters/network/kafka/kafka_protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

MetricsHolder::MetricsHolder(Stats::Scope& scope)
    : other_handler_{scope.counter("kafka.other.count"), scope.histogram("kafka.other.count")} {

  const std::vector<RequestSpec>& requests = KafkaRequest::requests();
  metrics_.reserve(requests.size());
  ToLowerTable lt;
  for (auto request : requests) {
    std::string ln = request.name_;
    lt.toLowerCase(ln);

    Stats::Counter& counter = scope.counter("kafka." + ln + ".count");
    Stats::Histogram& histogram = scope.histogram("kafka." + ln + ".duration");

    metrics_.push_back({counter, histogram});
  };
};

RequestMetrics& MetricsHolder::metric(INT16 api_key) {
  if (api_key >= 0 && api_key < static_cast<INT16>(metrics_.size())) {
    return metrics_[api_key];
  } else {
    return other_handler_;
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
