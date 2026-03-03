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

class RequestChunker {
public:
  RequestChunker(uint32_t max_dp, uint32_t max_rm);

  /**
   * Splits the given resource metrics into multiple export requests.
   * When max_dp is reached, the subsequent datapoints are split into a new request.
   * When max_rm is reached, the subsequent resource metrics are split into a new request.
   * If max_dp or max_rm is 0, the corresponding limit is disabled.
   */
  static std::vector<MetricsExportRequestPtr> chunkRequests(
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics>&
          resource_metrics,
      const uint32_t max_dp, const uint32_t max_rm);

  void submitRequestIfNeeded();
  void beginResourceMetric();
  void beginScopeMetric();
  void beginMetric();

  template <typename Callback>
  void appendDataPoint(const opentelemetry::proto::metrics::v1::ResourceMetrics& rm,
                       const opentelemetry::proto::metrics::v1::ScopeMetrics& sm,
                       const opentelemetry::proto::metrics::v1::Metric& metric,
                       Callback datapoint_callback) {
    if (max_dp_ > 0 && current_dp_count_ >= max_dp_) {
      submitRequestIfNeeded();
      current_rm_ = nullptr;
      current_sm_ = nullptr;
      current_metric_ = nullptr;
    }

    if (current_rm_ == nullptr) {
      if (max_rm_ > 0 && current_rm_count_ >= max_rm_) {
        submitRequestIfNeeded();
      }
      current_rm_ = current_request_->add_resource_metrics();
      current_rm_->mutable_resource()->CopyFrom(rm.resource());
      current_rm_->mutable_schema_url()->assign(rm.schema_url());
      current_rm_count_++;
    }

    if (current_sm_ == nullptr) {
      current_sm_ = current_rm_->add_scope_metrics();
      current_sm_->mutable_scope()->CopyFrom(sm.scope());
      current_sm_->mutable_schema_url()->assign(sm.schema_url());
    }

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
      } else if (metric.has_exponential_histogram()) {
        current_metric_->mutable_exponential_histogram()->set_aggregation_temporality(
            metric.exponential_histogram().aggregation_temporality());
      }
    }

    datapoint_callback(current_metric_);
    current_dp_count_++;
  }

  std::vector<MetricsExportRequestPtr> finish();

private:
  uint32_t max_dp_;
  uint32_t max_rm_;
  std::vector<MetricsExportRequestPtr> requests_;
  MetricsExportRequestPtr current_request_;
  uint32_t current_dp_count_{0};
  uint32_t current_rm_count_{0};

  opentelemetry::proto::metrics::v1::ResourceMetrics* current_rm_{nullptr};
  opentelemetry::proto::metrics::v1::ScopeMetrics* current_sm_{nullptr};
  opentelemetry::proto::metrics::v1::Metric* current_metric_{nullptr};
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
