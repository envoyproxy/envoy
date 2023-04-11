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

using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using MetricsExportResponse =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse;
using MetricsExportRequestPtr = std::unique_ptr<MetricsExportRequest>;
using SinkConfig = envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig;

class OtlpOptions {
public:
  OtlpOptions(const SinkConfig& sink_config);

  bool reportCountersAsDeltas() { return report_counters_as_deltas_; }
  bool reportHistogramsAsDeltas() { return report_histograms_as_deltas_; }
  bool emitTagsAsAttributes() { return emit_tags_as_attributes_; }
  bool useTagExtractedName() { return use_tag_extracted_name_; }

private:
  const bool report_counters_as_deltas_;
  const bool report_histograms_as_deltas_;
  const bool emit_tags_as_attributes_;
  const bool use_tag_extracted_name_;
};

using OtlpOptionsSharedPtr = std::shared_ptr<OtlpOptions>;

class OpenTelemetryGrpcMetricsExporter : public Grpc::AsyncRequestCallbacks<MetricsExportResponse> {
public:
  explicit OpenTelemetryGrpcMetricsExporter(const OtlpOptionsSharedPtr config,
                                            const Grpc::RawAsyncClientSharedPtr& raw_async_client)
      : config_(config), client_(raw_async_client) {}
  ~OpenTelemetryGrpcMetricsExporter() override = default;

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(MetricsExportRequestPtr&& metrics) PURE;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

protected:
  const OtlpOptionsSharedPtr config_;
  Grpc::AsyncClient<MetricsExportRequest, MetricsExportResponse> client_;
};

using GrpcMetricsExporterSharedPtr = std::shared_ptr<OpenTelemetryGrpcMetricsExporter>;

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
  const Protobuf::MethodDescriptor& service_method_;
};

using OpenTelemetryGrpcMetricsExporterImplPtr =
    std::unique_ptr<OpenTelemetryGrpcMetricsExporterImpl>;

class MetricsFlusher {
public:
  MetricsFlusher(const OtlpOptionsSharedPtr config,
                 std::function<bool(const Stats::Metric&)> predicate =
                     [](const auto& metric) { return metric.used(); })
      : config_(config), predicate_(predicate) {}

  MetricsExportRequestPtr flush(Stats::MetricSnapshot& snapshot) const;

private:
  void flushGauge(opentelemetry::proto::metrics::v1::Metric& metric, const Stats::Gauge& gauge,
                  int64_t snapshot_time_ns) const;

  void flushCounter(opentelemetry::proto::metrics::v1::Metric& metric,
                    const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
                    int64_t snapshot_time_ns) const;

  void flushHistogram(opentelemetry::proto::metrics::v1::Metric& metric,
                      const Stats::ParentHistogram& parent_histogram,
                      int64_t snapshot_time_ns) const;

  void setMetricCommon(opentelemetry::proto::metrics::v1::Metric& metric,
                       opentelemetry::proto::metrics::v1::NumberDataPoint& data_point,
                       int64_t snapshot_time_ns, const Stats::Metric& stat) const;

  void setMetricCommon(opentelemetry::proto::metrics::v1::Metric& metric,
                       opentelemetry::proto::metrics::v1::HistogramDataPoint& data_point,
                       int64_t snapshot_time_ns, const Stats::Metric& stat) const;

  const OtlpOptionsSharedPtr config_;
  const std::function<bool(const Stats::Metric&)> predicate_;
};

class OpenTelemetryGrpcSink : public Stats::Sink {
public:
  OpenTelemetryGrpcSink(const OtlpOptionsSharedPtr config,
                        const GrpcMetricsExporterSharedPtr& otlp_metrics_exporter)
      : flusher_(MetricsFlusher(config)), metrics_exporter_(otlp_metrics_exporter) {}

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override {
    metrics_exporter_->send(flusher_.flush(snapshot));
  }

  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  const MetricsFlusher flusher_;
  GrpcMetricsExporterSharedPtr metrics_exporter_;
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
