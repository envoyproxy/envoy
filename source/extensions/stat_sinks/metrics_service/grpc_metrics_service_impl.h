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

#include "common/buffer/buffer_impl.h"
#include "common/grpc/typed_async_client.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

/**
 * Interface for metrics streamer.
 */
class GrpcMetricsStreamer
    : public Grpc::AsyncStreamCallbacks<envoy::service::metrics::v3::StreamMetricsResponse> {
public:
  ~GrpcMetricsStreamer() override = default;

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(envoy::service::metrics::v3::StreamMetricsMessage& message) PURE;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void
  onReceiveMessage(std::unique_ptr<envoy::service::metrics::v3::StreamMetricsResponse>&&) override {
  }
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override{};
};

using GrpcMetricsStreamerSharedPtr = std::shared_ptr<GrpcMetricsStreamer>;

/**
 * Production implementation of GrpcMetricsStreamer
 */
class GrpcMetricsStreamerImpl : public Singleton::Instance, public GrpcMetricsStreamer {
public:
  GrpcMetricsStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory,
                          const LocalInfo::LocalInfo& local_info,
                          envoy::config::core::v3::ApiVersion transport_api_version);

  // GrpcMetricsStreamer
  void send(envoy::service::metrics::v3::StreamMetricsMessage& message) override;

  // Grpc::AsyncStreamCallbacks
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override { stream_ = nullptr; }

private:
  Grpc::AsyncStream<envoy::service::metrics::v3::StreamMetricsMessage> stream_{};
  Grpc::AsyncClient<envoy::service::metrics::v3::StreamMetricsMessage,
                    envoy::service::metrics::v3::StreamMetricsResponse>
      client_;
  const LocalInfo::LocalInfo& local_info_;
  const Protobuf::MethodDescriptor& service_method_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

using GrpcMetricsStreamerImplPtr = std::unique_ptr<GrpcMetricsStreamerImpl>;

/**
 * Stat Sink implementation of Metrics Service.
 */
class MetricsServiceSink : public Stats::Sink {
public:
  // MetricsService::Sink
  MetricsServiceSink(const GrpcMetricsStreamerSharedPtr& grpc_metrics_streamer,
                     TimeSource& time_system, const bool report_counters_as_deltas);
  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

  void flushCounter(const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot);
  void flushGauge(const Stats::Gauge& gauge);
  void flushHistogram(const Stats::ParentHistogram& envoy_histogram);

private:
  GrpcMetricsStreamerSharedPtr grpc_metrics_streamer_;
  envoy::service::metrics::v3::StreamMetricsMessage message_;
  TimeSource& time_source_;
  const bool report_counters_as_deltas_;
};

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
