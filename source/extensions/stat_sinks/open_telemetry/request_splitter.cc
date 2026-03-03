#include "source/extensions/stat_sinks/open_telemetry/request_splitter.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

RequestSplitter::RequestSplitter(uint32_t max_dp, uint32_t max_rm,
                                 const std::function<void(MetricsExportRequestPtr)>& send_callback)
    : max_dp_(max_dp), max_rm_(max_rm), send_callback_(send_callback),
      current_request_(std::make_unique<MetricsExportRequest>()) {}

void RequestSplitter::submitRequestIfNeeded() {
  if (current_dp_count_ > 0 || current_rm_count_ > 0) {
    send_callback_(std::move(current_request_));
    current_request_ = std::make_unique<MetricsExportRequest>();
    current_dp_count_ = 0;
    current_rm_count_ = 0;
  }
}

void RequestSplitter::beginResourceMetric() {
  if (max_rm_ > 0 && current_rm_count_ >= max_rm_) {
    submitRequestIfNeeded();
  }
  current_rm_ = nullptr;
}

void RequestSplitter::beginScopeMetric() { current_sm_ = nullptr; }

void RequestSplitter::beginMetric() { current_metric_ = nullptr; }

void RequestSplitter::finish() { submitRequestIfNeeded(); }

void RequestSplitter::chunkRequests(
    Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics>&
        resource_metrics,
    const uint32_t max_dp, const uint32_t max_rm,
    const std::function<void(MetricsExportRequestPtr)>& send_callback) {

  if (max_dp == 0 && max_rm == 0) {
    if (!resource_metrics.empty()) {
      auto request = std::make_unique<MetricsExportRequest>();
      request->mutable_resource_metrics()->Swap(&resource_metrics);
      send_callback(std::move(request));
    }
    return;
  }

  RequestSplitter chunker(max_dp, max_rm, send_callback);

  for (auto& rm : resource_metrics) {
    chunker.beginResourceMetric();
    for (auto& sm : *rm.mutable_scope_metrics()) {
      chunker.beginScopeMetric();
      for (auto& metric : *sm.mutable_metrics()) {
        chunker.beginMetric();
        switch (metric.data_case()) {
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kGauge:
          for (auto& dp : *metric.mutable_gauge()->mutable_data_points()) {
            chunker.appendDataPoint(rm, sm, metric, [&](auto* m) {
              *m->mutable_gauge()->add_data_points() = std::move(dp);
            });
          }
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kSum:
          for (auto& dp : *metric.mutable_sum()->mutable_data_points()) {
            chunker.appendDataPoint(rm, sm, metric, [&](auto* m) {
              *m->mutable_sum()->add_data_points() = std::move(dp);
            });
          }
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kHistogram:
          for (auto& dp : *metric.mutable_histogram()->mutable_data_points()) {
            chunker.appendDataPoint(rm, sm, metric, [&](auto* m) {
              *m->mutable_histogram()->add_data_points() = std::move(dp);
            });
          }
          break;
        default:
          break;
        }
      }
    }
  }

  chunker.finish();
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
