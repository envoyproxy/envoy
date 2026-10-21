#pragma once

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
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
 * The OTLP resources that the metrics of a flush are reported under, identified by index. The
 * resource at index 0 is the sink resource, built from the resource detectors. Each other
 * resource is added the first time that the resource conversion of a stat resolves to it.
 * Resources with the same attributes have the same index.
 */
class ResourceRegistry {
public:
  // The values of a stat for the tags of a resource conversion, in the order of
  // ConversionAction::ResourceConversion::tags.
  using TagValues = absl::InlinedVector<absl::string_view, 4>;

  // The index of the sink resource.
  static constexpr uint32_t SinkResourceIndex = 0;

  /**
   * @param sink_attributes supplies the attributes of the sink resource.
   */
  explicit ResourceRegistry(const Protobuf::RepeatedPtrField<KeyValue>& sink_attributes);

  /**
   * Returns the index of the resource that a resource conversion resolves to for a stat, adding
   * the resource if it is new.
   * @param conversion supplies the resource conversion. Must outlive the registry.
   * @param tag_values supplies the non-empty values of the stat for the tags of the conversion.
   */
  uint32_t findOrAdd(const ConversionAction::ResourceConversion& conversion,
                     const TagValues& tag_values);

  /**
   * @return the resource at the given index. The reference is invalidated by findOrAdd().
   */
  const opentelemetry::proto::resource::v1::Resource& get(uint32_t index) const {
    return resources_[index];
  }

private:
  using Attributes = std::vector<std::pair<std::string, std::string>>;

  // Returns the index of the resource with the given attributes, sorted by key, adding the
  // resource if it is new.
  uint32_t findOrAdd(Attributes&& sorted_attributes);

  std::vector<opentelemetry::proto::resource::v1::Resource> resources_;
  // Maps the sorted attributes of each resource to its index. The sink resource is only added
  // along with the first other resource, so that flushes without resource conversions don't pay
  // for it.
  absl::flat_hash_map<Attributes, uint32_t> indexes_by_attributes_;
  // Maps each resource conversion and values of its tags that a stat had to the index of the
  // resource that they resolve to, so that the attributes of a resource are only built once. The
  // tag values refer to the strings in tag_values_storage_.
  absl::flat_hash_map<std::pair<const ConversionAction::ResourceConversion*, TagValues>, uint32_t>
      indexes_by_tag_values_;
  // Owns the tag values in the keys of indexes_by_tag_values_. A deque keeps them in place as
  // values are added.
  std::deque<std::string> tag_values_storage_;
};

/**
 * This class helps to group data points by resource, metric name and attributes,
 * which is necessary for creating a valid OTLP request.
 */
class MetricAggregator : public Logger::Loggable<Logger::Id::stats> {
public:
  // Using a sorted vector of pairs instead of a hash map is more memory efficient
  // and faster for the small number of attributes (usually < 10) typical for metrics.
  // This also allows for faster linear comparisons when using this vector as a map key.
  using AttributesVector = absl::InlinedVector<std::pair<std::string, std::string>, 8>;
  class SortedAttributesVector {
  public:
    explicit SortedAttributesVector(AttributesVector&& vec) : sorted_attributes_(std::move(vec)) {
      ASSERT(std::is_sorted(sorted_attributes_.begin(), sorted_attributes_.end()));
    }
    SortedAttributesVector(std::initializer_list<std::pair<std::string, std::string>> list)
        : sorted_attributes_(list) {
      ASSERT(std::is_sorted(sorted_attributes_.begin(), sorted_attributes_.end()));
    }

    AttributesVector release() && { return std::move(sorted_attributes_); }
    bool operator==(const SortedAttributesVector& other) const {
      return sorted_attributes_ == other.sorted_attributes_;
    }

    template <typename H> friend H AbslHashValue(H h, const SortedAttributesVector& v) {
      return H::combine(std::move(h), v.sorted_attributes_);
    }

  private:
    AttributesVector sorted_attributes_;
  };

  explicit MetricAggregator(AggregationTemporality counter_temporality,
                            AggregationTemporality histogram_temporality)
      : counter_temporality_(counter_temporality), histogram_temporality_(histogram_temporality) {}

  class MetricKey {
  public:
    MetricKey(std::string&& name, SortedAttributesVector&& sorted_attributes)
        : name_(std::move(name)), sorted_attributes_(std::move(sorted_attributes)) {}

    bool operator==(const MetricKey& other) const {
      return name_ == other.name_ && sorted_attributes_ == other.sorted_attributes_;
    }

    template <typename H> friend H AbslHashValue(H h, const MetricKey& k) {
      return H::combine(std::move(h), k.name_, k.sorted_attributes_);
    }

    absl::string_view name() const { return name_; }
    const SortedAttributesVector& sortedAttributes() const { return sorted_attributes_; }

    std::string releaseName() { return std::move(name_); }
    SortedAttributesVector releaseAttributes() { return std::move(sorted_attributes_); }

  private:
    // Not const, so that releaseName() moves the name out of an extracted map node.
    std::string name_;
    SortedAttributesVector sorted_attributes_;
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
    absl::flat_hash_map<MetricKey, uint64_t> gauge_data_;
    absl::flat_hash_map<MetricKey, uint64_t> counter_data_;
    absl::flat_hash_map<MetricKey, CustomHistogram> histogram_data_;
  };

  // Returns the aggregation result of each resource, indexed by resource index.
  std::vector<AggregationResult> releaseResults();

  // Adds a gauge metric data point under the resource with the given index. Aggregates by summing
  // if a point with the same attributes exists.
  void addGauge(std::string&& metric_name, uint64_t value, SortedAttributesVector&& attributes,
                uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

  // Adds a counter metric data point under the resource with the given index. Aggregates by
  // summing if a point with the same attributes exists. The provided value should already respect
  // the configured temporality.
  void addCounter(std::string&& metric_name, uint64_t value, SortedAttributesVector&& attributes,
                  uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

  // Adds a histogram metric data point under the resource with the given index. Aggregates counts
  // and sums if a point with the same attributes and compatible bounds exists.
  void addHistogram(std::string&& metric_name, const Envoy::Stats::HistogramStatistics& stats,
                    SortedAttributesVector&& attributes,
                    uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

private:
  // Returns the aggregation result of the resource with the given index.
  AggregationResult& resultOf(uint32_t resource_index);

  // The aggregation result of each resource, indexed by resource index.
  std::vector<AggregationResult> results_;
  const AggregationTemporality counter_temporality_;
  const AggregationTemporality histogram_temporality_;
};

/**
 * Helper class to build ExportMetricsServiceRequest objects from AggregationResult.
 * It handles the batching of data points into requests, controlled by `max_dp_` per request.
 * Once a request reaches its data point limit, it is seamlessly dispatched to the provided
 * send_callback_. A request has one ResourceMetrics for each resource of its data points.
 */
class RequestStreamer {
public:
  /**
   * @param resources supplies the resources that data points are reported under. Must outlive
   *        the streamer.
   */
  RequestStreamer(uint32_t max_dp, const ResourceRegistry& resources,
                  opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality,
                  opentelemetry::proto::metrics::v1::AggregationTemporality histogram_temporality,
                  absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback,
                  int64_t snapshot_time_ns, int64_t delta_start_time_ns,
                  int64_t cumulative_start_time_ns, bool enable_metric_aggregation);

  // Adds a gauge metric data point under the resource with the given index to the streamer.
  void addGauge(std::string&& name, uint64_t value,
                MetricAggregator::SortedAttributesVector&& attributes,
                uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

  // Adds a counter metric data point under the resource with the given index to the streamer.
  void addCounter(std::string&& name, uint64_t value,
                  MetricAggregator::SortedAttributesVector&& attributes,
                  uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

  // Adds a custom histogram data point under the resource with the given index to the streamer.
  void addHistogram(std::string&& name, MetricAggregator::CustomHistogram hist,
                    MetricAggregator::SortedAttributesVector&& attributes,
                    uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

  // Adds a stats histogram data point under the resource with the given index to the streamer,
  // converting it first.
  void addHistogram(std::string&& name, const Envoy::Stats::HistogramStatistics& stats,
                    MetricAggregator::SortedAttributesVector&& attributes,
                    uint32_t resource_index = ResourceRegistry::SinkResourceIndex);

  // Adds all metrics from the aggregation results of the resources to the streamer, one resource
  // after another.
  void addAggregationResults(std::vector<MetricAggregator::AggregationResult>&& results);

  // Sends any current buffered metrics to the send callback.
  void send();

private:
  // Checks if the request limit is reached or if no request is active.
  // Sends the current request if full, and prepares a new one.
  void sendIfFullAndPrepareRequest();
  // Initializes a new MetricsExportRequest.
  void initNewRequest();

  // Returns the scope metrics of the resource with the given index in the current request, adding
  // the ResourceMetrics of the resource to the request if needed.
  ::opentelemetry::proto::metrics::v1::ScopeMetrics& scopeMetricsOf(uint32_t resource_index);

  // Finds or creates a metric in the scope metrics of the resource with the given index, using
  // zero-allocation lookups.
  ::opentelemetry::proto::metrics::v1::Metric* findOrCreateMetric(std::string&& name,
                                                                  uint32_t resource_index);

  // Sets common fields (timestamp, attributes) for a data point.
  template <class PointType>
  void setCommonFields(PointType* point, MetricAggregator::SortedAttributesVector&& attributes,
                       opentelemetry::proto::metrics::v1::AggregationTemporality temp) const;

  const uint32_t max_dp_;
  const bool enable_metric_aggregation_;
  const ResourceRegistry& resources_;
  const opentelemetry::proto::metrics::v1::AggregationTemporality counter_temporality_;
  const opentelemetry::proto::metrics::v1::AggregationTemporality histogram_temporality_;

  absl::AnyInvocable<void(MetricsExportRequestPtr)> send_callback_;
  const int64_t snapshot_time_ns_;
  const int64_t delta_start_time_ns_;
  const int64_t cumulative_start_time_ns_;

  std::unique_ptr<MetricsExportRequest> current_request_;
  // The scope metrics of each resource in the current request, indexed by resource index. Null if
  // the request has no data points of the resource.
  std::vector<::opentelemetry::proto::metrics::v1::ScopeMetrics*> resource_scope_metrics_;
  uint32_t dp_num_{0};
  // Maps resource index and metric name to the corresponding Metric object.
  // The string_view key points to the name owned by the Metric object (stored in
  // resource_scope_metrics_), ensuring zero-allocation lookups and insertions.
  absl::flat_hash_map<std::pair<uint32_t, absl::string_view>,
                      ::opentelemetry::proto::metrics::v1::Metric*>
      name_to_metric_;
};

class OtlpOptions {
public:
  /**
   * Creates the options of a sink.
   * @param sink_config supplies the config of the sink.
   * @param resource supplies the resource built from the resource detectors.
   * @param server supplies the server factory context.
   * @return the options, or an error status if the config is invalid.
   */
  static absl::StatusOr<std::shared_ptr<OtlpOptions>>
  create(const SinkConfig& sink_config, const Tracers::OpenTelemetry::Resource& resource,
         Server::Configuration::ServerFactoryContext& server);

  bool reportCountersAsDeltas() { return report_counters_as_deltas_; }
  bool reportHistogramsAsDeltas() { return report_histograms_as_deltas_; }
  bool emitTagsAsAttributes() { return emit_tags_as_attributes_; }
  bool useTagExtractedName() { return use_tag_extracted_name_; }
  absl::string_view statPrefix() { return stat_prefix_; }
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>&
  // NOLINTNEXTLINE(readability-identifier-naming)
  resource_attributes() const {
    return resource_attributes_;
  }

  const Envoy::Matcher::MatchTreeSharedPtr<Stats::StatMatchingData> matcher() const {
    return matcher_;
  }
  bool enableMetricAggregation() const { return enable_metric_aggregation_; }

  uint32_t maxDataPointsPerRequest() const { return max_data_points_per_request_; }

protected:
  OtlpOptions(const SinkConfig& sink_config, const Tracers::OpenTelemetry::Resource& resource,
              Server::Configuration::ServerFactoryContext& server, absl::Status& creation_status);

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
    // The matcher action that converts the stat, see stat_match_action.h, or null to convert the
    // stat without any customization.
    const ConversionAction* conversion_action{nullptr};
  };

  // The attributes of the data point of a stat, and the index of the resource that the data point
  // is reported under.
  struct DataPointAttributes {
    MetricAggregator::SortedAttributesVector attributes;
    uint32_t resource_index;
  };

private:
  template <class StatType> MetricConfig getMetricConfig(const StatType& stat) const;

  template <class StatType>
  std::string getMetricName(const StatType& stat, const ConversionAction* conversion_action) const;

  // Returns the attributes of the data point of a stat, along with the index of the resource that
  // the data point is reported under, which is added to `resources` if it is new.
  template <class StatType>
  DataPointAttributes getCombinedAttributes(const StatType& stat,
                                            const ConversionAction* conversion_action,
                                            ResourceRegistry& resources) const;

  /**
   * Processes all metrics (gauges, counters, histograms) from the snapshot and
   * adds them to the provided sink.
   * @param snapshot supplies the metrics snapshot to process.
   * @param resources supplies the resources of the flush, to which the resources of the metrics
   * are added.
   * @param sink supplies the sink to add the metrics to (e.g., MetricAggregator or
   * RequestStreamer).
   */
  template <typename SinkType>
  void sinkMetrics(Stats::MetricSnapshot& snapshot, ResourceRegistry& resources,
                   SinkType& sink) const;

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
