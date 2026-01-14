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
using MetricsExportRequestSharedPtr = std::shared_ptr<MetricsExportRequest>;
using SinkConfig = envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig;

/**
 * Aggregates individual metric data points into OTLP Metric protos.
 * This class helps to group data points by metric name and attributes,
 * which is necessary for creating a valid OTLP request.
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

    template <typename H> friend H AbslHashValue(H h, const DataPointKey& k) {
      return H::combine(std::move(h), k.attributes);
    }

    bool operator==(const DataPointKey& other) const { return attributes == other.attributes; }
  };

  // Holds the Metric proto and maps for quick lookups of data points.
  struct MetricData {
    ::opentelemetry::proto::metrics::v1::Metric metric;
    absl::flat_hash_map<DataPointKey, ::opentelemetry::proto::metrics::v1::NumberDataPoint*>
        gauge_points;
    absl::flat_hash_map<DataPointKey, ::opentelemetry::proto::metrics::v1::NumberDataPoint*>
        counter_points;
    absl::flat_hash_map<DataPointKey, ::opentelemetry::proto::metrics::v1::HistogramDataPoint*>
        histogram_points;
  };

  // Adds a gauge metric data point. Aggregates by summing if a point with the
  // same attributes exists.
  void addGauge(
      absl::string_view metric_name, int64_t value,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes);

  // Adds a counter metric data point. Aggregates by summing the delta or value
  // based on temporality if a point with the same attributes exists.
  void addCounter(
      absl::string_view metric_name, uint64_t value, uint64_t delta,
      ::opentelemetry::proto::metrics::v1::AggregationTemporality temporality,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes);

  // Adds a histogram metric data point. Aggregates counts and sums if a point
  // with the same attributes and compatible bounds exists.
  void addHistogram(
      absl::string_view stat_name, absl::string_view metric_name,
      const Stats::HistogramStatistics& stats,
      ::opentelemetry::proto::metrics::v1::AggregationTemporality temporality,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attributes);

  // Returns a RepeatedPtrField of ResourceMetrics containing all aggregated
  // metrics.
  Protobuf::RepeatedPtrField<::opentelemetry::proto::metrics::v1::ResourceMetrics>
  getResourceMetrics(const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
                         resource_attributes) const;

private:
  // Converts a RepeatedPtrField of KeyValue to an AttributesMap.
  static AttributesMap GetAttributesMap(
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>& attrs);

  // Gets or creates a MetricData object for a given metric name.
  MetricData& getOrCreateMetric(absl::string_view metric_name);

  // Sets common fields for a data point.
  // For gauge metrics,
  // temporality should be AGGREGATION_TEMPORALITY_UNSPECIFIED.
  // For the aggregation case, attributes should be nullptr as they have already been
  // set.
  template <typename DataPoint>
  void setCommonDataPoint(
      DataPoint& data_point,
      const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>* attributes,
      ::opentelemetry::proto::metrics::v1::AggregationTemporality temporality) {
    data_point.set_time_unix_nano(snapshot_time_ns_);
    if (attributes) {
      // When attributes are present, set the start time for delta/cumulative metrics.
      data_point.mutable_attributes()->CopyFrom(*attributes);
      switch (temporality) {
      case AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA:
        data_point.set_start_time_unix_nano(delta_start_time_ns_);
        break;
      case AggregationTemporality::AGGREGATION_TEMPORALITY_CUMULATIVE:
        data_point.set_start_time_unix_nano(cumulative_start_time_ns_);
        break;
      default:
        // Do not set start time for UNSPECIFIED.
        break;
      }
    }
  }

  const bool enable_metric_aggregation_;
  const int64_t snapshot_time_ns_;
  const int64_t delta_start_time_ns_;
  const int64_t cumulative_start_time_ns_;
  absl::flat_hash_map<std::string, MetricData> metrics_;

  // Currently, the metrics without defined in `custom_metric_conversions` won't be aggregated and
  // will be directly stored in this list.
  std::vector<::opentelemetry::proto::metrics::v1::Metric> non_aggregated_metrics_;
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

private:
  const bool report_counters_as_deltas_;
  const bool report_histograms_as_deltas_;
  const bool emit_tags_as_attributes_;
  const bool use_tag_extracted_name_;
  const std::string stat_prefix_;
  bool enable_metric_aggregation_;
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> resource_attributes_;
  const Envoy::Matcher::MatchTreeSharedPtr<Stats::StatMatchingData> matcher_;
};

using OtlpOptionsSharedPtr = std::shared_ptr<OtlpOptions>;

class OtlpMetricsFlusher {
public:
  virtual ~OtlpMetricsFlusher() = default;

  /**
   * Creates an OTLP export request from metric snapshot.
   * @param snapshot supplies the metrics snapshot to send.
   */
  virtual MetricsExportRequestPtr flush(Stats::MetricSnapshot& snapshot,
                                        int64_t delta_start_time_ns,
                                        int64_t cumulative_start_time_ns) const PURE;
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

  MetricsExportRequestPtr flush(Stats::MetricSnapshot& snapshot, int64_t delta_start_time_ns,
                                int64_t cumulative_start_time_ns) const override;

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
    metrics_exporter_->send(
        metrics_flusher_->flush(snapshot, last_flush_time_ns_, proxy_start_time_ns_));
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
