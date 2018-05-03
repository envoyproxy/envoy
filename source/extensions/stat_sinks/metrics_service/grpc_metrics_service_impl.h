#pragma once

#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/service/metrics/v2/metrics_service.pb.h"
#include "envoy/service/metrics/v2/metrics_service.pb.validate.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

// TODO: Move the common code to a base class so that Accesslog and Metrics Service can reuse.

/**
 * Interface for metrics streamer. The streamer deals with threading and sends
 * metrics on the correct stream.
 */
class GrpcMetricsStreamer {
public:
  virtual ~GrpcMetricsStreamer() {}

  /**
   * Send Metrics Message.
   * @param message supplies the metrics to send.
   */
  virtual void send(envoy::service::metrics::v2::StreamMetricsMessage& message) PURE;
};

typedef std::shared_ptr<GrpcMetricsStreamer> GrpcMetricsStreamerSharedPtr;

/**
 * Production implementation of GrpcAccessLogStreamer that supports per-thread
 * streams
 */
class GrpcMetricsStreamerImpl : public Singleton::Instance, public GrpcMetricsStreamer {
public:
  GrpcMetricsStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory, ThreadLocal::SlotAllocator& tls,
                          const LocalInfo::LocalInfo& local_info);

  // GrpcMetricsStreamer
  void send(envoy::service::metrics::v2::StreamMetricsMessage& message) override {
    tls_slot_->getTyped<ThreadLocalStreamer>().send(message);
  }

private:
  /**
   * Shared state that is owned by the per-thread streamers. This allows the
   * main streamer/TLS slot to be destroyed while the streamers hold onto the shared state.
   */
  struct SharedState {
    SharedState(Grpc::AsyncClientFactoryPtr&& factory, const LocalInfo::LocalInfo& local_info)
        : factory_(std::move(factory)), local_info_(local_info) {}

    Grpc::AsyncClientFactoryPtr factory_;
    const LocalInfo::LocalInfo& local_info_;
  };

  typedef std::shared_ptr<SharedState> SharedStateSharedPtr;

  struct ThreadLocalStreamer;

  /**
   * Per-thread stream state.
   */
  struct ThreadLocalStream
      : public Grpc::TypedAsyncStreamCallbacks<envoy::service::metrics::v2::StreamMetricsResponse> {
    ThreadLocalStream(ThreadLocalStreamer& parent) : parent_(parent) {}

    // Grpc::TypedAsyncStreamCallbacks
    void onCreateInitialMetadata(Http::HeaderMap&) override {}
    void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override {}
    void onReceiveMessage(
        std::unique_ptr<envoy::service::metrics::v2::StreamMetricsResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

    ThreadLocalStreamer& parent_;
    Grpc::AsyncStream* stream_{};
  };

  typedef std::shared_ptr<ThreadLocalStream> ThreadLocalStreamSharedPtr;

  /**
   * Per-thread multi-stream state.
   */
  struct ThreadLocalStreamer : public ThreadLocal::ThreadLocalObject {
    ThreadLocalStreamer(const SharedStateSharedPtr& shared_state);
    void send(envoy::service::metrics::v2::StreamMetricsMessage& message);

    Grpc::AsyncClientPtr client_;
    ThreadLocalStreamSharedPtr thread_local_stream_ = nullptr;
    SharedStateSharedPtr shared_state_;
  };

  ThreadLocal::SlotPtr tls_slot_;
};
/**
 * Per thread implementation of a Metric Service flusher.
 */
class MetricsServiceSink : public Stats::Sink {
public:
  // MetricsService::Sink
  MetricsServiceSink(const GrpcMetricsStreamerSharedPtr& grpc_metrics_streamer);

  void beginFlush() override { message_.clear_envoy_metrics(); }

  void flushCounter(const Stats::Counter& counter, uint64_t) override;
  void flushGauge(const Stats::Gauge& gauge, uint64_t value) override;
  void flushHistogram(const Stats::ParentHistogram& histogram) override;

  void endFlush() override {
    grpc_metrics_streamer_->send(message_);
    // for perf reasons, clear the identifer after the first flush.
    if (message_.has_identifier()) {
      message_.clear_identifier();
    }
  }

  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  GrpcMetricsStreamerSharedPtr grpc_metrics_streamer_;
  envoy::service::metrics::v2::StreamMetricsMessage message_;
};

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
