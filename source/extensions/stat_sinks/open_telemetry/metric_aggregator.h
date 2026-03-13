#pragma once

#include "envoy/common/optref.h"
#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "source/common/common/logger.h"

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using AggregationTemporality = opentelemetry::proto::metrics::v1::AggregationTemporality;
using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using MetricsExportRequestPtr = std::unique_ptr<MetricsExportRequest>;
using MetricsExportRequestSharedPtr = std::shared_ptr<MetricsExportRequest>;

/**
 * Aggregates individual metric data points into OTLP Metric protos.
 * This class helps to group data points by metric name and attributes,
 * which is necessary for creating a valid OTLP request.
 */
class MetricAggregator : public Logger::Loggable<Logger::Id::stats> {
public:
  using AttributesMap = absl::flat_hash_map<std::string, std::string>;

  explicit MetricAggregator(
      bool enable_metric_aggregation, int64_t snapshot_time_ns,
      int64_t delta_start_time_ns, int64_t cumulative_start_time_ns,
      uint32_t max_dp,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& resource_attributes)
      : enable_metric_aggregation_(enable_metric_aggregation), snapshot_time_ns_(snapshot_time_ns),
        delta_start_time_ns_(delta_start_time_ns),
        cumulative_start_time_ns_(cumulative_start_time_ns),
        max_dp_(max_dp), resource_attributes_(resource_attributes) {}

  // Key used to group data points by their attributes.
  struct DataPointKey {
    AttributesMap attributes;

    template <typename H> friend H AbslHashValue(H h, const DataPointKey& k) {
      return H::combine(std::move(h), k.attributes);
    }

    bool operator==(const DataPointKey& other) const { return attributes == other.attributes; }
  };

  // Holds maps for quick lookups of data points.
  struct MetricData {
    absl::flat_hash_map<DataPointKey, ::opentelemetry::proto::metrics::v1::NumberDataPoint*>
        gauge_points;
    absl::flat_hash_map<DataPointKey, ::opentelemetry::proto::metrics::v1::NumberDataPoint*>
        sum_points;
    absl::flat_hash_map<DataPointKey, ::opentelemetry::proto::metrics::v1::HistogramDataPoint*>
        histogram_points;
  };

  std::vector<MetricsExportRequestPtr> releaseRequests() { return std::move(requests_); }

  void addGauge(const std::string& metric_name, uint64_t value,
                const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
                    attributes);
  void addCounter(absl::string_view metric_name, uint64_t value, uint64_t delta,
                  AggregationTemporality temporality,
                  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
                      attributes);
  void addHistogram(
      const std::string& metric_name, const Envoy::Stats::HistogramStatistics& stats,
      AggregationTemporality temporality,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes);

private:
  MetricData& getOrCreateMetric(absl::string_view metric_name);
  opentelemetry::proto::metrics::v1::Metric*
  getOrCreateMetricInCurrentRequest(absl::string_view metric_name);
  void createNewRequest();
  template <typename DataPoint>
  void setCommonDataPoint(
      DataPoint& data_point,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes,
      AggregationTemporality temporality) const;

  const bool enable_metric_aggregation_;
  const int64_t snapshot_time_ns_;
  const int64_t delta_start_time_ns_;
  const int64_t cumulative_start_time_ns_;
  const uint32_t max_dp_;
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& resource_attributes_;

  absl::flat_hash_map<std::string, MetricData> metrics_;
  std::vector<MetricsExportRequestPtr> requests_;
  std::vector<opentelemetry::proto::metrics::v1::Metric*> non_aggregated_metrics_;
  opentelemetry::proto::metrics::v1::ScopeMetrics* current_scope_metrics_{nullptr};
  absl::flat_hash_map<std::string, opentelemetry::proto::metrics::v1::Metric*>
      current_request_metrics_;
  uint32_t cur_dp_num_{0};
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
