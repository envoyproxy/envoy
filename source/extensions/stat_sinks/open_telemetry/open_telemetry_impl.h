#pragma once

#include <memory>

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

class OpenTelemetryGrpcMetricsExporter : public Grpc::AsyncRequestCallbacks<MetricsExportResponse> {
public:
  explicit OpenTelemetryGrpcMetricsExporter(const Grpc::RawAsyncClientSharedPtr& raw_async_client)
      : client_(raw_async_client) {}
  ~OpenTelemetryGrpcMetricsExporter() override = default;

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(MetricsExportRequestPtr&& metrics) PURE;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

protected:
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
  OpenTelemetryGrpcMetricsExporterImpl(Grpc::RawAsyncClientSharedPtr raw_async_client);

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
  MetricsFlusher(
      bool report_counters_as_deltas, bool report_histograms_as_deltas,
      bool emit_tags_as_attributes, bool use_tag_extracted_name,
      std::function<bool(const Stats::Metric&)> predicate =
          [](const auto& metric) { return metric.used(); })
      : report_counters_as_deltas_(report_counters_as_deltas),
        report_histograms_as_deltas_(report_histograms_as_deltas),
        emit_tags_as_attributes_(emit_tags_as_attributes),
        use_tag_extracted_name_(use_tag_extracted_name), predicate_(predicate) {}

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

  const bool report_counters_as_deltas_;
  const bool report_histograms_as_deltas_;
  const bool emit_tags_as_attributes_;
  const bool use_tag_extracted_name_;
  const std::function<bool(const Stats::Metric&)> predicate_;
};

class OpenTelemetryGrpcSink : public Stats::Sink {
public:
  OpenTelemetryGrpcSink(const GrpcMetricsExporterSharedPtr& otlp_metrics_exporter,
                        bool report_counters_as_deltas, bool report_histograms_as_deltas,
                        bool emit_tags_as_attributes, bool use_tag_extracted_name)
      : flusher_(MetricsFlusher(report_counters_as_deltas, report_histograms_as_deltas,
                                emit_tags_as_attributes, use_tag_extracted_name)),
        metrics_exporter_(otlp_metrics_exporter) {}

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
