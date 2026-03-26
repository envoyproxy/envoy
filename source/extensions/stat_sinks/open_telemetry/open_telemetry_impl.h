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
using MetricsExportRequestSharedPtr = std::shared_ptr<MetricsExportRequest>;
using SinkConfig = envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig;

/**
 * This class helps to group data points by metric name and attributes,
 * which is necessary for creating a valid OTLP request.
 */
class MetricAggregator : public Logger::Loggable<Logger::Id::stats> {
public:
  // Using a sorted vector of pairs instead of a hash map is more memory efficient
  // and faster for the small number of attributes (usually < 10) typical for metrics.
  // This also allows for faster linear comparisons when using this vector as a map key.
  using AttributesVector = std::vector<std::pair<std::string, std::string>>;

  explicit MetricAggregator(int64_t snapshot_time_ns, int64_t delta_start_time_ns,
                            int64_t cumulative_start_time_ns,
                            AggregationTemporality counter_temporality,
                            AggregationTemporality histogram_temporality)
      : snapshot_time_ns_(snapshot_time_ns), delta_start_time_ns_(delta_start_time_ns),
        cumulative_start_time_ns_(cumulative_start_time_ns),
        counter_temporality_(counter_temporality), histogram_temporality_(histogram_temporality) {}

  struct MetricKey {
    // Full name of the metric.
    std::string name_;
    // Attributes associated with the data points for this metric.
    AttributesVector attributes_;

    MetricKey(absl::string_view name, AttributesVector attributes)
        : name_(name), attributes_(std::move(attributes)) {}

    bool operator==(const MetricKey& other) const {
      return name_ == other.name_ && attributes_ == other.attributes_;
    }

    template <typename H> friend H AbslHashValue(H h, const MetricKey& k) {
      return H::combine(std::move(h), k.name_, k.attributes_);
    }
  };

  // MetricViewKey is a temporary view of MetricKey that does not own the name or attributes.
  // It is used for zero-allocation heterogeneous searches in the map (e.g., using `find()`).
  // Since `MetricKeyHash` and `MetricKeyEqual` are marked as transparent, the map can compute
  // the view's hash and compare it directly against the stored `MetricKey` without allocating
  // memory to create a `MetricKey` just to search the map.
  struct MetricViewKey {
    absl::string_view name_;
    const AttributesVector& attributes_;

    MetricViewKey(absl::string_view name, const AttributesVector& attributes)
        : name_(name), attributes_(attributes) {}

    bool operator==(const MetricViewKey& other) const {
      return name_ == other.name_ && attributes_ == other.attributes_;
    }

    template <typename H> friend H AbslHashValue(H h, const MetricViewKey& v) {
      return H::combine(std::move(h), v.name_, v.attributes_);
    }
  };

  struct MetricKeyHash {
    using is_transparent = void;

    size_t operator()(const MetricKey& k) const { return absl::Hash<MetricKey>{}(k); }

    size_t operator()(const MetricViewKey& v) const { return absl::Hash<MetricViewKey>{}(v); }
  };

  struct MetricKeyEqual {
    using is_transparent = void;

    bool operator()(const MetricKey& a, const MetricKey& b) const { return a == b; }

    bool operator()(const MetricKey& a, const MetricViewKey& b) const {
      return a.name_ == b.name_ && a.attributes_ == b.attributes_;
    }

    bool operator()(const MetricViewKey& a, const MetricKey& b) const {
      return a.name_ == b.name_ && a.attributes_ == b.attributes_;
    }
  };

  struct CustomHistogram {
    // Total number of data points.
    uint64_t count_;
    // Sum of all data point values.
    double sum_;
    // Vector of counts for each bucket.
    std::vector<uint64_t> bucket_counts_;
    // Vector of upper bounds for each bucket.
    std::vector<double> explicit_bounds_;
  };

  // Maps a unique combination of metric name and attributes to their data point.
  struct AggregationResult {
    absl::flat_hash_map<MetricKey, uint64_t, MetricKeyHash, MetricKeyEqual> gauge_data_;
    absl::flat_hash_map<MetricKey, uint64_t, MetricKeyHash, MetricKeyEqual> counter_data_;
    absl::flat_hash_map<MetricKey, CustomHistogram, MetricKeyHash, MetricKeyEqual> histogram_data_;
    // Timestamp for the snapshot.
    int64_t snapshot_time_ns_;
    // Start time for cumulative metrics.
    int64_t cumulative_start_time_ns_;
    // Start time for delta metrics.
    int64_t delta_start_time_ns_;
  };

  AggregationResult releaseResult();

  // Adds a gauge metric data point. Aggregates by summing if a point with the
  // same attributes exists.
  void addGauge(absl::string_view metric_name, uint64_t value, AttributesVector attributes);

  // Adds a counter metric data point. Aggregates by summing the delta or value
  // based on temporality if a point with the same attributes exists.
  void addCounter(absl::string_view metric_name, uint64_t value, uint64_t delta,
                  AttributesVector attributes);

  // Adds a histogram metric data point. Aggregates counts and sums if a point
  // with the same attributes and compatible bounds exists.
  void addHistogram(absl::string_view metric_name, const Envoy::Stats::HistogramStatistics& stats,
                    AttributesVector attributes);

private:
  const int64_t snapshot_time_ns_;
  const int64_t delta_start_time_ns_;
  const int64_t cumulative_start_time_ns_;

  absl::flat_hash_map<MetricKey, uint64_t, MetricKeyHash, MetricKeyEqual> gauge_data_;
  absl::flat_hash_map<MetricKey, uint64_t, MetricKeyHash, MetricKeyEqual> counter_data_;
  absl::flat_hash_map<MetricKey, CustomHistogram, MetricKeyHash, MetricKeyEqual> histogram_data_;
  const AggregationTemporality counter_temporality_;
  const AggregationTemporality histogram_temporality_;
};

/**
 * Helper class to build ExportMetricsServiceRequest objects from AggregationResult.
 * It handles the batching of data points into requests, controlled by max_dp_ per request.
 * Once a request reaches its data point limit, it is seamlessly dispatched to the provided
 * send_callback_.
 */
class RequestStreamer {
public:
  RequestStreamer(uint32_t max_dp,
                  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
                      resource_attributes,
                  opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality,
                  opentelemetry::proto::metrics::v1::AggregationTemporality histogram_temporality,
                  absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback,
                  int64_t snapshot_time_ns, int64_t delta_start_time_ns,
                  int64_t cumulative_start_time_ns);

  // Adds a gauge metric data point to the streamer.
  void addGauge(absl::string_view name, uint64_t value,
                MetricAggregator::AttributesVector attributes);

  // Adds a counter metric data point to the streamer.
  void addCounter(absl::string_view name, uint64_t value, uint64_t delta,
                  MetricAggregator::AttributesVector attributes);

  // Adds a custom histogram data point to the streamer.
  void addHistogram(absl::string_view name, MetricAggregator::CustomHistogram hist,
                    MetricAggregator::AttributesVector attributes);

  // Adds a stats histogram data point to the streamer, converting it first.
  void addHistogram(absl::string_view name, const Envoy::Stats::HistogramStatistics& stats,
                    MetricAggregator::AttributesVector attributes);

  // Adds all metrics from an aggregation result to the streamer.
  void addAggregationResult(MetricAggregator::AggregationResult&& result);

  // Sends any current buffered metrics to the send callback.
  void send();

private:
  // Checks if the request limit is reached, and sends if necessary.
  void sendIfFull();
  // Initializes a new MetricsExportRequest.
  void initNewRequest();

  // Finds or creates a metric in the current scope metrics, using zero-allocation lookups.
  ::opentelemetry::proto::metrics::v1::Metric* findOrCreateMetric(absl::string_view name);

  // Sets common fields (timestamp, attributes) for a data point.
  template <class PointType>
  void setCommonFields(PointType* point, MetricAggregator::AttributesVector attributes,
                       opentelemetry::proto::metrics::v1::AggregationTemporality temp) const;

  const uint32_t max_dp_;
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
      resource_attributes_;
  const opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality_;
  const opentelemetry::proto::metrics::v1::AggregationTemporality histogram_temporality_;

  absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback_;
  const int64_t snapshot_time_ns_;
  const int64_t delta_start_time_ns_;
  const int64_t cumulative_start_time_ns_;

  std::unique_ptr<MetricsExportRequest> current_request_;
  ::opentelemetry::proto::metrics::v1::ScopeMetrics* current_scope_metrics_{nullptr};
  uint32_t dp_num_{0};
  // Maps metric name to its corresponding Metric object.
  // The string_view key points to the name owned by the Metric object (stored in
  // current_scope_metrics_), ensuring zero-allocation lookups and insertions.
  absl::flat_hash_map<absl::string_view, ::opentelemetry::proto::metrics::v1::Metric*>
      name_to_metric_;
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

  uint32_t maxDataPointsPerRequest() const { return max_data_points_per_request_; }

private:
  const bool report_counters_as_deltas_;
  const bool report_histograms_as_deltas_;
  const bool emit_tags_as_attributes_;
  const bool use_tag_extracted_name_;
  const std::string stat_prefix_;
  bool enable_metric_aggregation_;
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> resource_attributes_;
  const Envoy::Matcher::MatchTreeSharedPtr<Stats::StatMatchingData> matcher_;
  const uint32_t max_data_points_per_request_;
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
      : config_(config), predicate_(predicate),
        counter_temporality_(config->reportCountersAsDeltas()
                                 ? opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_DELTA
                                 : opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_CUMULATIVE),
        histogram_temporality_(config->reportHistogramsAsDeltas()
                                   ? opentelemetry::proto::metrics::v1::AggregationTemporality::
                                         AGGREGATION_TEMPORALITY_DELTA
                                   : opentelemetry::proto::metrics::v1::AggregationTemporality::
                                         AGGREGATION_TEMPORALITY_CUMULATIVE) {}

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
  MetricAggregator::AttributesVector
  getCombinedAttributes(const StatType& stat,
                        OptRef<const SinkConfig::ConversionAction> conversion_config) const;

  /**
   * Processes all metrics (gauges, counters, histograms) from the snapshot and
   * adds them to the provided sink.
   * @param snapshot supplies the metrics snapshot to process.
   * @param sink supplies the sink to add the metrics to (e.g., MetricAggregator or
   * RequestStreamer).
   */
  template <typename SinkType>
  void sinkMetrics(Stats::MetricSnapshot& snapshot, SinkType& sink) const;

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
  const opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality_;
  const opentelemetry::proto::metrics::v1::AggregationTemporality histogram_temporality_;
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
