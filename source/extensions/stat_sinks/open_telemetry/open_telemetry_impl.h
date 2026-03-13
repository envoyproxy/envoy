#pragma once

#include <memory>

#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"
#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.validate.h"
#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/common/matchers.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using AggregationTemporality = opentelemetry::proto::metrics::v1::AggregationTemporality;
using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using MetricsExportResponse =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse;
using KeyValue = opentelemetry::proto::common::v1::KeyValue;
using MetricsExportRequestPtr = std::unique_ptr<MetricsExportRequest>;
using SinkConfig = envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig;

class MetricAggregator;

/**
 * This class helps to group data points by metric name and attributes,
 * which is necessary for creating a valid OTLP request.
 *
 * If `enable_metric_aggregation_` is true:
 * - Gauge metrics: We overwrite the existing data point for the same attributes.
 * - Counter metrics: We sum the data points if the temporality is delta. Otherwise, we overwrite
 * the existing data point.
 * - Histogram metrics: We aggregate the counts and sums if bounds are compatible.
 *
 * Data points are also split into multiple OTLP requests if the number of
 * data points exceeds `max_datapoints_per_request_`. A new request is spawned
 * when the limit is reached.
 */
class MetricAggregator : public Logger::Loggable<Logger::Id::stats> {
public:
  using AttributesMap = absl::flat_hash_map<std::string, std::string>;

  explicit MetricAggregator(bool enable_metric_aggregation, int64_t snapshot_time_ns,
                            int64_t delta_start_time_ns, int64_t cumulative_start_time_ns)
      : enable_metric_aggregation_(enable_metric_aggregation), snapshot_time_ns_(snapshot_time_ns),
        delta_start_time_ns_(delta_start_time_ns),
        cumulative_start_time_ns_(cumulative_start_time_ns) {}

  // Key used to group data points by their attributes.
  struct DataPointKey {
    AttributesMap attributes;

    DataPointKey() = default;

    explicit DataPointKey(
        const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& kvs) {
      for (const auto& attr : kvs) {
        attributes.emplace(attr.key(), attr.value().string_value());
      }
    }

    template <typename H> friend H AbslHashValue(H h, const DataPointKey& k) {
      return H::combine(std::move(h), k.attributes);
    }

    bool operator==(const DataPointKey& other) const { return attributes == other.attributes; }
  };
  // Internal representation of aggregated and unaggregated metrics, grouping data
  // points by their temporality and key before they are flushed to OpenTelemetry requests.
  struct MetricDataPoints {
    std::vector<::opentelemetry::proto::metrics::v1::NumberDataPoint> gauge_points_data;
    absl::flat_hash_map<DataPointKey, size_t> gauge_points_indices;

    std::vector<::opentelemetry::proto::metrics::v1::NumberDataPoint> sum_points_data;
    absl::flat_hash_map<DataPointKey, size_t> sum_points_indices;
    AggregationTemporality sum_temporality{
        AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED};

    std::vector<::opentelemetry::proto::metrics::v1::HistogramDataPoint> histogram_points_data;
    absl::flat_hash_map<DataPointKey, size_t> histogram_points_indices;
    AggregationTemporality histogram_temporality{
        AggregationTemporality::AGGREGATION_TEMPORALITY_UNSPECIFIED};

    std::vector<::opentelemetry::proto::metrics::v1::NumberDataPoint> non_aggregated_gauge_points;
    std::vector<::opentelemetry::proto::metrics::v1::NumberDataPoint> non_aggregated_sum_points;
    std::vector<::opentelemetry::proto::metrics::v1::HistogramDataPoint>
        non_aggregated_histogram_points;
  };
  absl::flat_hash_map<std::string, MetricDataPoints> releaseResult();

  // Adds a gauge metric data point. Aggregates by summing if a point with the
  // same attributes exists.
  void addGauge(const std::string& metric_name, uint64_t value,
                Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes);

  // Adds a counter metric data point. Aggregates by summing the delta or value
  // based on temporality if a point with the same attributes exists.
  void
  addCounter(absl::string_view metric_name, uint64_t value, uint64_t delta,
             AggregationTemporality temporality,
             Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes);

  // Adds a histogram metric data point. Aggregates counts and sums if a point
  // with the same attributes and compatible bounds exists.
  void
  addHistogram(const std::string& metric_name, const Envoy::Stats::HistogramStatistics& stats,
               AggregationTemporality temporality,
               Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes);

private:
  MetricDataPoints& getOrCreateMetricDataPoints(absl::string_view metric_name);
  template <typename DataPoint>
  void setCommonDataPoint(
      DataPoint& data_point,
      Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes,
      AggregationTemporality temporality) const;

  void configureHistogramPoint(
      ::opentelemetry::proto::metrics::v1::HistogramDataPoint& data_point,
      Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes,
      const Envoy::Stats::HistogramStatistics& stats, AggregationTemporality temporality) const;

  const bool enable_metric_aggregation_;
  const int64_t snapshot_time_ns_;
  const int64_t delta_start_time_ns_;
  const int64_t cumulative_start_time_ns_;

  absl::flat_hash_map<std::string, MetricDataPoints> name_to_dps_;
};

/**
 * Helper class to build ExportMetricsServiceRequest objects from MetricDataPoints.
 * It handles the batching of data points into requests, controlled by max_dp_ per request.
 * Once a request reaches its data point limit, it is seamlessly dispatched to the provided
 * send_callback_.
 */
class RequestBuilder {
public:
  RequestBuilder(bool enable_metric_aggregation, uint32_t max_dp,
                 const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
                     resource_attributes,
                 absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback)
      : enable_metric_aggregation_(enable_metric_aggregation), max_dp_(max_dp),
        resource_attributes_(resource_attributes), send_callback_(std::move(send_callback)) {}

  void buildRequests(absl::flat_hash_map<std::string, MetricAggregator::MetricDataPoints>& metrics);

private:
  void ensureRequest();

  /**
   * Helper method to populate Gauge data points into the metrics request.
   * @param metric_name the name of the metric.
   * @param datapoints the vector of NumberDataPoint items to add.
   * @param unaggregated_split whether to force split these data points into separate metric entries
   * (used for unaggregated points).
   */
  void handleGaugePointsList(
      const std::string& metric_name,
      std::vector<::opentelemetry::proto::metrics::v1::NumberDataPoint>& datapoints,
      bool unaggregated_split);

  /**
   * Helper method to populate Sum data points into the metrics request.
   * @param metric_name the name of the metric.
   * @param container the vector of NumberDataPoint items to add.
   * @param unaggregated_split whether to force split these data points into separate metric entries
   * (used for unaggregated points).
   * @param temporality the aggregation temporality.
   */
  void
  handleSumPointsList(const std::string& metric_name,
                      std::vector<::opentelemetry::proto::metrics::v1::NumberDataPoint>& datapoints,
                      bool unaggregated_split,
                      opentelemetry::proto::metrics::v1::AggregationTemporality temporality);

  /**
   * Helper method to populate Histogram data points into the metrics request.
   * @param metric_name the name of the metric.
   * @param datapoints the vector of HistogramDataPoint items to add.
   * @param unaggregated_split whether to force split these data points into separate metric entries
   * (used for unaggregated points).
   * @param temporality the aggregation temporality.
   */
  void handleHistogramPointsList(
      const std::string& metric_name,
      std::vector<::opentelemetry::proto::metrics::v1::HistogramDataPoint>& datapoints,
      bool unaggregated_split,
      opentelemetry::proto::metrics::v1::AggregationTemporality temporality);

  bool enable_metric_aggregation_;
  const uint32_t max_dp_;
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
      resource_attributes_;
  absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback_;

  MetricsExportRequestPtr current_request_{nullptr};
  opentelemetry::proto::metrics::v1::ScopeMetrics* current_scope_metrics_{nullptr};
  uint32_t dp_num_{0};
};

class OtlpOptions {
public:
  OtlpOptions(const SinkConfig& sink_config, const Tracers::OpenTelemetry::Resource& resource,
              Server::Configuration::ServerFactoryContext& server);

  bool reportCountersAsDeltas() { return report_counters_as_deltas_; }
  bool reportHistogramsAsDeltas() { return report_histograms_as_deltas_; }
  bool emitTagsAsAttributes() { return emit_tags_as_attributes_; }
  bool useTagExtractedName() { return use_tag_extracted_name_; }
  absl::string_view statPrefix() { return stat_prefix_; }
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
  resource_attributes() const {
    return resource_attributes_;
  }

  const Envoy::Matcher::MatchTreeSharedPtr<Stats::StatMatchingData> matcher() const {
    return matcher_;
  }
  bool enableMetricAggregation() const { return enable_metric_aggregation_; }

  uint32_t maxDatapointsPerRequest() const { return max_datapoints_per_request_; }

private:
  const bool report_counters_as_deltas_;
  const bool report_histograms_as_deltas_;
  const bool emit_tags_as_attributes_;
  const bool use_tag_extracted_name_;
  const std::string stat_prefix_;
  bool enable_metric_aggregation_;
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> resource_attributes_;
  const Envoy::Matcher::MatchTreeSharedPtr<Stats::StatMatchingData> matcher_;
  const uint32_t max_datapoints_per_request_;
};

using OtlpOptionsSharedPtr = std::shared_ptr<OtlpOptions>;

class OtlpMetricsFlusher {
public:
  virtual ~OtlpMetricsFlusher() = default;

  /**
   * Creates an OTLP export request from metric snapshot.
   * @param snapshot supplies the metrics snapshot to send.
   * @param delta_start_time_ns supplies the start time for the delta aggregation.
   * @param cumulative_start_time_ns supplies the start time for the cumulative aggregation.
   * @param send_callback supplies the callback to invoke to send a single metrics export request.
   */
  virtual void flush(Stats::MetricSnapshot& snapshot, int64_t delta_start_time_ns,
                     int64_t cumulative_start_time_ns,
                     absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) const PURE;
};

using OtlpMetricsFlusherSharedPtr = std::shared_ptr<OtlpMetricsFlusher>;

/**
 * Production implementation of OtlpMetricsFlusher
 */
class OtlpMetricsFlusherImpl : public OtlpMetricsFlusher,
                               public Logger::Loggable<Logger::Id::stats> {
public:
  OtlpMetricsFlusherImpl(
      const OtlpOptionsSharedPtr config, std::function<bool(const Stats::Metric&)> predicate =
                                             [](const auto& metric) { return metric.used(); })
      : config_(config), predicate_(predicate) {}

  void flush(Stats::MetricSnapshot& snapshot, int64_t delta_start_time_ns,
             int64_t cumulative_start_time_ns,
             absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback) const override;

private:
  struct MetricConfig {
    bool drop_stat{false};
    OptRef<const SinkConfig::ConversionAction> conversion_action;
  };

private:
  template <class StatType> MetricConfig getMetricConfig(const StatType& stat) const;

  template <class StatType>
  std::string getMetricName(const StatType& stat,
                            OptRef<const SinkConfig::ConversionAction> conversion_config) const;

  template <class StatType>
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>
  getCombinedAttributes(const StatType& stat,
                        OptRef<const SinkConfig::ConversionAction> conversion_config) const;
  template <class GaugeType>
  void addGaugeDataPoint(opentelemetry::proto::metrics::v1::Metric& metric,
                         const GaugeType& gauge_stat, int64_t snapshot_time_ns) const;

  template <class CounterType>
  void addCounterDataPoint(opentelemetry::proto::metrics::v1::Metric& metric,
                           const CounterType& counter, uint64_t value, uint64_t delta,
                           int64_t snapshot_time_ns) const;

  void addHistogramDataPoint(opentelemetry::proto::metrics::v1::Metric& metric,
                             const Stats::ParentHistogram& parent_histogram,
                             int64_t snapshot_time_ns) const;

  template <class StatType>
  void setMetricCommon(opentelemetry::proto::metrics::v1::NumberDataPoint& data_point,
                       int64_t snapshot_time_ns, const StatType& stat) const;

  void setMetricCommon(opentelemetry::proto::metrics::v1::HistogramDataPoint& data_point,
                       int64_t snapshot_time_ns, const Stats::Metric& stat) const;

  const OtlpOptionsSharedPtr config_;
  const std::function<bool(const Stats::Metric&)> predicate_;
};

/**
 * Abstract base class for OTLP metrics exporters.
 */
class OtlpMetricsExporter {
public:
  virtual ~OtlpMetricsExporter() = default;

  /**
   * Send metrics to the configured OTLP service.
   * @param metrics the OTLP metrics export request.
   */
  virtual void send(MetricsExportRequestPtr&& metrics) PURE;
};

using OtlpMetricsExporterSharedPtr = std::shared_ptr<OtlpMetricsExporter>;

/**
 * gRPC implementation of OtlpMetricsExporter.
 */
class OpenTelemetryGrpcMetricsExporter : public OtlpMetricsExporter,
                                         public Grpc::AsyncRequestCallbacks<MetricsExportResponse> {
public:
  ~OpenTelemetryGrpcMetricsExporter() override = default;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
};

using OpenTelemetryGrpcMetricsExporterSharedPtr = std::shared_ptr<OpenTelemetryGrpcMetricsExporter>;

/**
 * Production implementation of OpenTelemetryGrpcMetricsExporter
 */
class OpenTelemetryGrpcMetricsExporterImpl : public Singleton::Instance,
                                             public OpenTelemetryGrpcMetricsExporter,
                                             public Logger::Loggable<Logger::Id::stats> {
public:
  OpenTelemetryGrpcMetricsExporterImpl(const OtlpOptionsSharedPtr config,
                                       Grpc::RawAsyncClientSharedPtr raw_async_client);

  // OpenTelemetryGrpcMetricsExporter
  void send(MetricsExportRequestPtr&& metrics) override;

  // Grpc::AsyncRequestCallbacks
  void onSuccess(Grpc::ResponsePtr<MetricsExportResponse>&&, Tracing::Span&) override;
  void onFailure(Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&) override;

private:
  const OtlpOptionsSharedPtr config_;
  Grpc::AsyncClient<MetricsExportRequest, MetricsExportResponse> client_;
  const Protobuf::MethodDescriptor& service_method_;
};

using OpenTelemetryGrpcMetricsExporterImplPtr =
    std::unique_ptr<OpenTelemetryGrpcMetricsExporterImpl>;

/**
 * Stats sink that exports metrics via OTLP (gRPC or HTTP).
 */
class OpenTelemetrySink : public Stats::Sink {
public:
  OpenTelemetrySink(const OtlpMetricsFlusherSharedPtr& otlp_metrics_flusher,
                    const OtlpMetricsExporterSharedPtr& metrics_exporter, int64_t create_time_ns)
      : metrics_flusher_(otlp_metrics_flusher), metrics_exporter_(metrics_exporter),
        // Use the time when the sink is created as the last flush time for the first flush.
        last_flush_time_ns_(create_time_ns), proxy_start_time_ns_(create_time_ns) {}

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override {
    const int64_t current_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        snapshot.snapshotTime().time_since_epoch())
                                        .count();
    metrics_flusher_->flush(
        snapshot, last_flush_time_ns_, proxy_start_time_ns_,
        [this](MetricsExportRequestPtr request) { metrics_exporter_->send(std::move(request)); });
    last_flush_time_ns_ = current_time_ns;
  }

  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  const OtlpMetricsFlusherSharedPtr metrics_flusher_;
  const OtlpMetricsExporterSharedPtr metrics_exporter_;
  int64_t last_flush_time_ns_;
  int64_t proxy_start_time_ns_;
};
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
