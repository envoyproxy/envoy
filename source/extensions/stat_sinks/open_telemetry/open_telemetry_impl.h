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

#include "source/common/grpc/typed_async_client.h"

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

class OtlpOptions {
public:
  OtlpOptions(const SinkConfig& sink_config);

  bool reportCountersAsDeltas() { return report_counters_as_deltas_; }
  bool reportHistogramsAsDeltas() { return report_histograms_as_deltas_; }
  bool emitTagsAsAttributes() { return emit_tags_as_attributes_; }
  bool useTagExtractedName() { return use_tag_extracted_name_; }
  const std::string& statPrefix() { return stat_prefix_; }

private:
  const bool report_counters_as_deltas_;
  const bool report_histograms_as_deltas_;
  const bool emit_tags_as_attributes_;
  const bool use_tag_extracted_name_;
  const std::string stat_prefix_;
};

using OtlpOptionsSharedPtr = std::shared_ptr<OtlpOptions>;

class OtlpMetricsFlusher {
public:
  virtual ~OtlpMetricsFlusher() = default;

  /**
   * Creates an OTLP export request from metric snapshot.
   * @param snapshot supplies the metrics snapshot to send.
   */
  virtual MetricsExportRequestPtr flush(Stats::MetricSnapshot& snapshot) const PURE;
};

using OtlpMetricsFlusherSharedPtr = std::shared_ptr<OtlpMetricsFlusher>;

/**
 * Production implementation of OtlpMetricsFlusher
 */
class OtlpMetricsFlusherImpl : public OtlpMetricsFlusher {
public:
  OtlpMetricsFlusherImpl(
      const OtlpOptionsSharedPtr config, std::function<bool(const Stats::Metric&)> predicate =
                                             [](const auto& metric) { return metric.used(); })
      : config_(config), predicate_(predicate) {}

  MetricsExportRequestPtr flush(Stats::MetricSnapshot& snapshot) const override;

private:
  template <class GaugeType>
  void flushGauge(opentelemetry::proto::metrics::v1::Metric& metric, const GaugeType& gauge,
                  int64_t snapshot_time_ns) const;

  template <class CounterType>
  void flushCounter(opentelemetry::proto::metrics::v1::Metric& metric, const CounterType& counter,
                    uint64_t value, uint64_t delta, int64_t snapshot_time_ns) const;

  void flushHistogram(opentelemetry::proto::metrics::v1::Metric& metric,
                      const Stats::ParentHistogram& parent_histogram,
                      int64_t snapshot_time_ns) const;

  template <class StatType>
  void setMetricCommon(opentelemetry::proto::metrics::v1::Metric& metric,
                       opentelemetry::proto::metrics::v1::NumberDataPoint& data_point,
                       int64_t snapshot_time_ns, const StatType& stat) const;

  void setMetricCommon(opentelemetry::proto::metrics::v1::Metric& metric,
                       opentelemetry::proto::metrics::v1::HistogramDataPoint& data_point,
                       int64_t snapshot_time_ns, const Stats::Metric& stat) const;

  const OtlpOptionsSharedPtr config_;
  const std::function<bool(const Stats::Metric&)> predicate_;
};

class OpenTelemetryGrpcMetricsExporter : public Grpc::AsyncRequestCallbacks<MetricsExportResponse> {
public:
  ~OpenTelemetryGrpcMetricsExporter() override = default;

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(MetricsExportRequestPtr&& metrics) PURE;

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

class OpenTelemetryGrpcSink : public Stats::Sink {
public:
  OpenTelemetryGrpcSink(const OtlpMetricsFlusherSharedPtr& otlp_metrics_flusher,
                        const OpenTelemetryGrpcMetricsExporterSharedPtr& grpc_metrics_exporter)
      : metrics_flusher_(otlp_metrics_flusher), metrics_exporter_(grpc_metrics_exporter) {}

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override {
    metrics_exporter_->send(metrics_flusher_->flush(snapshot));
  }

  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  const OtlpMetricsFlusherSharedPtr metrics_flusher_;
  const OpenTelemetryGrpcMetricsExporterSharedPtr metrics_exporter_;
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
