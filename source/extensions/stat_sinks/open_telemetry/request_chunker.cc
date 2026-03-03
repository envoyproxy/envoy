#include "source/extensions/stat_sinks/open_telemetry/request_chunker.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

RequestChunker::RequestChunker(uint32_t max_dp, uint32_t max_rm)
    : max_dp_(max_dp), max_rm_(max_rm), current_request_(std::make_unique<MetricsExportRequest>()) {
}

void RequestChunker::submitRequestIfNeeded() {
  if (current_dp_count_ > 0 || current_rm_count_ > 0) {
    requests_.push_back(std::move(current_request_));
    current_request_ = std::make_unique<MetricsExportRequest>();
    current_dp_count_ = 0;
    current_rm_count_ = 0;
  }
}

void RequestChunker::beginResourceMetric() {
  if (max_rm_ > 0 && current_rm_count_ >= max_rm_) {
    submitRequestIfNeeded();
  }
  current_rm_ = nullptr;
}

void RequestChunker::beginScopeMetric() { current_sm_ = nullptr; }

void RequestChunker::beginMetric() { current_metric_ = nullptr; }

std::vector<MetricsExportRequestPtr> RequestChunker::finish() {
  submitRequestIfNeeded();
  if (requests_.empty()) {
    requests_.push_back(std::move(current_request_));
  }
  return std::move(requests_);
}

std::vector<MetricsExportRequestPtr> RequestChunker::chunkRequests(
    const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics>&
        resource_metrics,
    const uint32_t max_dp, const uint32_t max_rm) {

  RequestChunker chunker(max_dp, max_rm);

  for (const auto& rm : resource_metrics) {
    chunker.beginResourceMetric();
    for (const auto& sm : rm.scope_metrics()) {
      chunker.beginScopeMetric();
      for (const auto& metric : sm.metrics()) {
        chunker.beginMetric();
        switch (metric.data_case()) {
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kGauge:
          for (const auto& dp : metric.gauge().data_points()) {
            chunker.appendDataPoint(rm, sm, metric,
                                    [&](auto* m) { *m->mutable_gauge()->add_data_points() = dp; });
          }
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kSum:
          for (const auto& dp : metric.sum().data_points()) {
            chunker.appendDataPoint(rm, sm, metric,
                                    [&](auto* m) { *m->mutable_sum()->add_data_points() = dp; });
          }
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kHistogram:
          for (const auto& dp : metric.histogram().data_points()) {
            chunker.appendDataPoint(
                rm, sm, metric, [&](auto* m) { *m->mutable_histogram()->add_data_points() = dp; });
          }
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kExponentialHistogram:
          for (const auto& dp : metric.exponential_histogram().data_points()) {
            chunker.appendDataPoint(rm, sm, metric, [&](auto* m) {
              *m->mutable_exponential_histogram()->add_data_points() = dp;
            });
          }
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kSummary:
          for (const auto& dp : metric.summary().data_points()) {
            chunker.appendDataPoint(
                rm, sm, metric, [&](auto* m) { *m->mutable_summary()->add_data_points() = dp; });
          }
          break;
        default:
          break;
        }
      }
    }
  }

  return chunker.finish();
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
