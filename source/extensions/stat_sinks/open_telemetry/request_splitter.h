#pragma once

#include <memory>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using MetricsExportRequestPtr = std::unique_ptr<MetricsExportRequest>;

class RequestSplitter {
public:
  /**
   * Splits the given resource metrics into multiple export requests based on configured limits.
   * When max_dp is reached, the subsequent data points are split into a new request.
   * When max_rm is reached, the subsequent resource metrics are split into a new request.
   * If max_dp and max_rm are both 0, the corresponding limit is disabled.
   */
  static void
  chunkRequests(Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics>&
                    resource_metrics,
                const uint32_t max_dp, const uint32_t max_rm,
                const std::function<void(MetricsExportRequestPtr)>& send_callback);

private:
  RequestSplitter(uint32_t max_dp, uint32_t max_rm,
                  const std::function<void(MetricsExportRequestPtr)>& send_callback);

  void submitRequestIfNeeded();

  // Signal the start of processing a new ResourceMetrics.
  void beginResourceMetric();
  // Signal the start of processing a new ScopeMetrics.
  void beginScopeMetric();
  // Signal the start of processing a new Metric.
  void beginMetric();

  // Appends a single data point to the current request.
  // This validates the max data points limit first, then recursively ensures that
  // current_rm_, current_sm_, and current_metric_ are properly initialized within
  // the current request limit constraints before calling datapoint_callback.
  template <typename DataPointCallback>
  void appendDataPoint(const opentelemetry::proto::metrics::v1::ResourceMetrics& rm,
                       const opentelemetry::proto::metrics::v1::ScopeMetrics& sm,
                       const opentelemetry::proto::metrics::v1::Metric& metric,
                       DataPointCallback datapoint_callback) {
    // If the maximum data point limit is reached, submit the current request
    // and clear all context pointers so they can be reconstructed in the new request.
    if (max_dp_ > 0 && current_dp_count_ >= max_dp_) {
      submitRequestIfNeeded();
      current_rm_ = nullptr;
      current_sm_ = nullptr;
      current_metric_ = nullptr;
    }

    // If we're starting a new resource metric (e.g. after a submit or passing from another rm),
    // ensure we reconstruct it in the current request.
    if (current_rm_ == nullptr) {
      current_rm_ = current_request_->add_resource_metrics();
      current_rm_->mutable_resource()->CopyFrom(rm.resource());
      current_rm_->mutable_schema_url()->assign(rm.schema_url());
      current_rm_count_++;
    }

    // If this is the start of a new scope metric, initialize it inside the current resource metric.
    if (current_sm_ == nullptr) {
      current_sm_ = current_rm_->add_scope_metrics();
      current_sm_->mutable_scope()->CopyFrom(sm.scope());
      current_sm_->mutable_schema_url()->assign(sm.schema_url());
    }

    // If this is the start of a new metric, copy its definition into the current scope metric.
    if (current_metric_ == nullptr) {
      current_metric_ = current_sm_->add_metrics();
      current_metric_->set_name(metric.name());
      current_metric_->set_description(metric.description());
      current_metric_->set_unit(metric.unit());
      if (metric.has_sum()) {
        current_metric_->mutable_sum()->set_aggregation_temporality(
            metric.sum().aggregation_temporality());
        current_metric_->mutable_sum()->set_is_monotonic(metric.sum().is_monotonic());
      } else if (metric.has_histogram()) {
        current_metric_->mutable_histogram()->set_aggregation_temporality(
            metric.histogram().aggregation_temporality());
      }
    }

    datapoint_callback(current_metric_);
    current_dp_count_++;
  }

  void finish();

private:
  // Maximum number of data points per export request.
  uint32_t max_dp_;
  // Maximum number of resource metrics per export request.
  uint32_t max_rm_;
  // Callback invoked to send a constructed export request.
  std::function<void(MetricsExportRequestPtr)> send_callback_;

  // The current export request being built.
  MetricsExportRequestPtr current_request_;
  // Number of data points accumulated in the current request.
  uint32_t current_dp_count_{0};
  // Number of resource metrics accumulated in the current request.
  uint32_t current_rm_count_{0};

  // Pointers to the active mutable structures in the current request being built.
  // These are used to append nested metrics and data points without repeatedly looking them up.
  opentelemetry::proto::metrics::v1::ResourceMetrics* current_rm_{nullptr};
  opentelemetry::proto::metrics::v1::ScopeMetrics* current_sm_{nullptr};
  opentelemetry::proto::metrics::v1::Metric* current_metric_{nullptr};
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
