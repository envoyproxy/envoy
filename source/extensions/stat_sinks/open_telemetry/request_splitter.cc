#include "source/extensions/stat_sinks/open_telemetry/request_splitter.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

void RequestSplitter::chunkRequests(
    Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics>& rm_list,
    const uint32_t max_dp, const std::function<void(MetricsExportRequestPtr)>& send_callback) {

  // Envoy's OTLP stat sink currently guarantees exactly 1 ResourceMetrics
  // and exactly 1 ScopeMetrics per export loop. We optimize the chunking
  // by holding this assumption.
  if (rm_list.empty()) {
    return;
  }
  ENVOY_BUG(rm_list.size() == 1, "Expected exactly 1 ResourceMetrics in OTLP stat sink.");
  auto* rm = rm_list.Mutable(0);
  if (rm->scope_metrics_size() == 0) {
    return;
  }
  ENVOY_BUG(rm->scope_metrics_size() == 1,
            "Expected exactly 1 ScopeMetrics in OTLP stat sink ResourceMetrics.");
  auto* sm = rm->mutable_scope_metrics(0);

  if (max_dp == 0) {
    auto current_request = std::make_unique<MetricsExportRequest>();
    current_request->mutable_resource_metrics()->Add()->Swap(rm);
    send_callback(std::move(current_request));
    return;
  }

  auto base_request = std::make_unique<MetricsExportRequest>();
  auto* base_rm = base_request->add_resource_metrics();
  base_rm->mutable_resource()->CopyFrom(rm->resource());
  base_rm->mutable_schema_url()->assign(rm->schema_url());

  auto* base_sm = base_rm->add_scope_metrics();
  base_sm->mutable_scope()->CopyFrom(sm->scope());
  base_sm->mutable_schema_url()->assign(sm->schema_url());

  MetricsExportRequestPtr current_request = std::make_unique<MetricsExportRequest>(*base_request);
  uint32_t cur_dp = 0;

  for (auto& metric : *sm->mutable_metrics()) {
    opentelemetry::proto::metrics::v1::Metric* current_metric = nullptr;

    auto append_dp = [&send_callback, &base_request, &current_request, &cur_dp, &current_metric,
                      max_dp, &metric](
                         const std::function<void(opentelemetry::proto::metrics::v1::Metric*)>&
                             add_dp_func) {
      if (cur_dp == max_dp) {
        send_callback(std::move(current_request));
        current_request = std::make_unique<MetricsExportRequest>(*base_request);
        cur_dp = 0;
        current_metric = nullptr;
      }

      if (current_metric == nullptr) {
        current_metric =
            current_request->mutable_resource_metrics(0)->mutable_scope_metrics(0)->add_metrics();
        // Copy the metric metadata (name, description, unit, data type oneof) to the new chunk.
        // The data points themselves will be cleared out below so that we can append only
        // the remaining data points for this specific chunk.
        *current_metric = metric;
        switch (metric.data_case()) {
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kGauge:
          current_metric->mutable_gauge()->clear_data_points();
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kSum:
          current_metric->mutable_sum()->clear_data_points();
          break;
        case opentelemetry::proto::metrics::v1::Metric::DataCase::kHistogram:
          current_metric->mutable_histogram()->clear_data_points();
          break;
        default:
          break;
        }
      }

      add_dp_func(current_metric);
      cur_dp++;
    };

    switch (metric.data_case()) {
    case opentelemetry::proto::metrics::v1::Metric::DataCase::kGauge:
      for (auto& dp : *metric.mutable_gauge()->mutable_data_points()) {
        append_dp([&](auto* m) { *m->mutable_gauge()->add_data_points() = std::move(dp); });
      }
      break;
    case opentelemetry::proto::metrics::v1::Metric::DataCase::kSum:
      for (auto& dp : *metric.mutable_sum()->mutable_data_points()) {
        append_dp([&](auto* m) { *m->mutable_sum()->add_data_points() = std::move(dp); });
      }
      break;
    case opentelemetry::proto::metrics::v1::Metric::DataCase::kHistogram:
      for (auto& dp : *metric.mutable_histogram()->mutable_data_points()) {
        append_dp([&](auto* m) { *m->mutable_histogram()->add_data_points() = std::move(dp); });
      }
      break;
    case opentelemetry::proto::metrics::v1::Metric::DataCase::kExponentialHistogram:
    case opentelemetry::proto::metrics::v1::Metric::DataCase::kSummary:
      ENVOY_BUG(false, "ExponentialHistogram and Summary metric types are not supported");
      break;
    default:
      break;
    }
  }

  if (cur_dp > 0) {
    send_callback(std::move(current_request));
  }
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
