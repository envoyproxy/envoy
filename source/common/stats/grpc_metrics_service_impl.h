#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"

#include "api/metrics_service.pb.h"
#include "api/metrics_service.pb.validate.h"

namespace Envoy {
namespace Stats {
namespace Metrics {

typedef std::unique_ptr<Grpc::AsyncClient<
    envoy::api::v2::StreamMetricsMessage,
    envoy::api::v2::StreamMetricsResponse>> GrpcMetricsServiceClientPtr;

/**
 * Factory for creating a gRPC metrics service streaming client.
 */
class GrpcMetricsServiceClientFactory {
 public:
  virtual ~GrpcMetricsServiceClientFactory() {}

  /**
   * @return GrpcMetricsServiceClientPtr a new client.
   */
  virtual GrpcMetricsServiceClientPtr create() PURE;
};

typedef std::unique_ptr<GrpcMetricsServiceClientFactory>
    GrpcMetricsServiceClientFactoryPtr;

/**
 * Interface for metrics streamer. The streamer deals with threading and sends
 * metrics
 * on the correct stream.
 */
class GrpcMetricsStreamer {
 public:
  virtual ~GrpcMetricsStreamer() {}

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(envoy::api::v2::StreamMetricsMessage& message) PURE;
};

typedef std::shared_ptr<GrpcMetricsStreamer> GrpcMetricsStreamerSharedPtr;

/**
 * Production implementation of GrpcAccessLogStreamer that supports per-thread
 * streams
 */
class GrpcMetricsStreamerImpl : public Singleton::Instance,
                                public GrpcMetricsStreamer {
 public:
  GrpcMetricsStreamerImpl(GrpcMetricsServiceClientFactoryPtr&& factory,
                          ThreadLocal::SlotAllocator& tls,
                          const LocalInfo::LocalInfo& local_info);

  // GrpcMetricsStreamer
  void send(envoy::api::v2::StreamMetricsMessage& message) override {
    tls_slot_->getTyped<ThreadLocalStreamer>().send(message);
  }

 private:
  /**
   * Shared state that is owned by the per-thread streamers. This allows the
   * main streamer/TLS
   * slot to be destroyed while the streamers hold onto the shared state.
   */
  struct SharedState {
    SharedState(GrpcMetricsServiceClientFactoryPtr&& factory,
                const LocalInfo::LocalInfo& local_info)
        : factory_(std::move(factory)), local_info_(local_info) {}

    GrpcMetricsServiceClientFactoryPtr factory_;
    const LocalInfo::LocalInfo& local_info_;
  };

  typedef std::shared_ptr<SharedState> SharedStateSharedPtr;

  struct ThreadLocalStreamer;

  /**
   * Per-thread stream state.
   */
  struct ThreadLocalStream : public Grpc::AsyncStreamCallbacks<
                                 envoy::api::v2::StreamMetricsResponse> {
    ThreadLocalStream(ThreadLocalStreamer& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::HeaderMap&) override {}
    void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override {}
    void onReceiveMessage(
        std::unique_ptr<envoy::api::v2::StreamMetricsResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus status,
                       const std::string& message) override;

    ThreadLocalStreamer& parent_;
    Grpc::AsyncStream<envoy::api::v2::StreamMetricsMessage>* stream_{};
  };

  /**
   * Per-thread multi-stream state.
   */
  struct ThreadLocalStreamer : public ThreadLocal::ThreadLocalObject {
    ThreadLocalStreamer(const SharedStateSharedPtr& shared_state);
    void send(envoy::api::v2::StreamMetricsMessage& message);

    GrpcMetricsServiceClientPtr client_;
    // TODO(ramachavali): Map is not required as there is only one entry.
    std::unordered_map<std::string, ThreadLocalStream> stream_map_;
    SharedStateSharedPtr shared_state_;
  };

  ThreadLocal::SlotPtr tls_slot_;
};
/**
 * Per thread implementation of a Metric Service flusher.
 */
class MetricsServiceSink : public Sink {
 public:
  MetricsServiceSink(const LocalInfo::LocalInfo& local_info,
                     const std::string& cluster_name,
                     ThreadLocal::SlotAllocator& tls,
                     Upstream::ClusterManager& cluster_manager,
                     Stats::Scope& scope,
                     GrpcMetricsStreamerSharedPtr grpc_metrics_streamer);

  // MetricsService::Sink
  void beginFlush() override { message.clear_envoy_metrics(); }

  void flushCounter(const Counter& counter, uint64_t delta) override {
    std::cout << "Delta:" << delta << "\n";
    io::prometheus::client::MetricFamily* metrics_family =
        message.add_envoy_metrics();
    metrics_family->set_name(counter.name());
    auto* metric = metrics_family->add_metric();
    auto* counter_metric = metric->mutable_counter();
    counter_metric->set_value(counter.value());
  }

  void flushGauge(const Gauge& gauge, uint64_t value) override {
    io::prometheus::client::MetricFamily* metrics_family =
        message.add_envoy_metrics();
    metrics_family->set_name(gauge.name());
    auto* metric = metrics_family->add_metric();
    auto* gauage_metric = metric->mutable_gauge();
    gauage_metric->set_value(value);
  }

  void endFlush() override { grpc_metrics_streamer_->send(message); }

  void onHistogramComplete(const Histogram& histogram,
                           uint64_t value) override {
    // TODO(ramaraochavali): Need to figure out how map existing histogram to
    // Proto Model
    std::cout << "Histogram Called" << histogram.name() << "value:" << value
              << "\n";
  }

 private:
  GrpcMetricsStreamerSharedPtr grpc_metrics_streamer_;
  envoy::api::v2::StreamMetricsMessage message;

  // TODO(ramaraochavali): check some of these things are not required
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  ThreadLocal::SlotPtr tls_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::Counter& cx_overflow_stat_;
};

}  // namespace Metrics
}  // namespace Stats
}  // namespace Envoy