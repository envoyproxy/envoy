#pragma once

#include <memory>

#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/typed_async_client.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

using MetricsPtr =
    std::unique_ptr<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>;

/**
 * Interface for metrics streamer.
 */
template <class RequestProto, class ResponseProto>
class GrpcMetricsStreamer : public Grpc::AsyncStreamCallbacks<ResponseProto> {
public:
  explicit GrpcMetricsStreamer(const Grpc::RawAsyncClientSharedPtr& raw_async_client)
      : client_(raw_async_client) {}
  ~GrpcMetricsStreamer() override = default;

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(MetricsPtr&& metrics) PURE;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveMessage(std::unique_ptr<ResponseProto>&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override{};

protected:
  Grpc::AsyncStream<RequestProto> stream_{};
  Grpc::AsyncClient<RequestProto, ResponseProto> client_;
};

template <class RequestProto, class ResponseProto>
using GrpcMetricsStreamerSharedPtr =
    std::shared_ptr<GrpcMetricsStreamer<RequestProto, ResponseProto>>;

/**
 * Production implementation of GrpcMetricsStreamer
 */
class GrpcMetricsStreamerImpl
    : public Singleton::Instance,
      public GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsMessage,
                                 envoy::service::metrics::v3::StreamMetricsResponse> {
public:
  GrpcMetricsStreamerImpl(Grpc::RawAsyncClientSharedPtr raw_async_client,
                          const LocalInfo::LocalInfo& local_info);

  // GrpcMetricsStreamer
  void send(MetricsPtr&& metrics) override;

  // Grpc::AsyncStreamCallbacks
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override { stream_ = nullptr; }

private:
  const LocalInfo::LocalInfo& local_info_;
  const Protobuf::MethodDescriptor& service_method_;
};

using GrpcMetricsStreamerImplPtr = std::unique_ptr<GrpcMetricsStreamerImpl>;

class MetricsFlusher {
public:
  MetricsFlusher(
      bool report_counters_as_deltas, bool emit_labels,
      std::function<bool(const Stats::Metric&)> predicate =
          [](const auto& metric) { return metric.used(); })
      : report_counters_as_deltas_(report_counters_as_deltas), emit_labels_(emit_labels),
        predicate_(predicate) {}

  MetricsPtr flush(Stats::MetricSnapshot& snapshot) const;

private:
  void flushCounter(io::prometheus::client::MetricFamily& metrics_family,
                    const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
                    int64_t snapshot_time_ms) const;
  void flushGauge(io::prometheus::client::MetricFamily& metrics_family, const Stats::Gauge& gauge,
                  int64_t snapshot_time_ms) const;
  void flushHistogram(io::prometheus::client::MetricFamily& summary_metrics_family,
                      io::prometheus::client::MetricFamily& histogram_metrics_family,
                      const Stats::ParentHistogram& envoy_histogram,
                      int64_t snapshot_time_ms) const;

  io::prometheus::client::Metric*
  populateMetricsFamily(io::prometheus::client::MetricFamily& metrics_family,
                        io::prometheus::client::MetricType type, int64_t snapshot_time_ms,
                        const Stats::Metric& metric) const;

  const bool report_counters_as_deltas_;
  const bool emit_labels_;
  const std::function<bool(const Stats::Metric&)> predicate_;
};

/**
 * Stat Sink that flushes metrics via a gRPC service.
 */
template <class RequestProto, class ResponseProto> class MetricsServiceSink : public Stats::Sink {
public:
  MetricsServiceSink(
      const GrpcMetricsStreamerSharedPtr<RequestProto, ResponseProto>& grpc_metrics_streamer,
      bool report_counters_as_deltas, bool emit_labels)
      : MetricsServiceSink(grpc_metrics_streamer,
                           MetricsFlusher(report_counters_as_deltas, emit_labels)) {}

  MetricsServiceSink(
      const GrpcMetricsStreamerSharedPtr<RequestProto, ResponseProto>& grpc_metrics_streamer,
      MetricsFlusher&& flusher)
      : flusher_(std::move(flusher)), grpc_metrics_streamer_(std::move(grpc_metrics_streamer)) {}

  // MetricsService::Sink
  void flush(Stats::MetricSnapshot& snapshot) override {
    grpc_metrics_streamer_->send(flusher_.flush(snapshot));
  }
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  const MetricsFlusher flusher_;
  GrpcMetricsStreamerSharedPtr<RequestProto, ResponseProto> grpc_metrics_streamer_;
};

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
