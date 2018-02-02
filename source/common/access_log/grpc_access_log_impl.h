#pragma once

#include <unordered_map>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/filter/accesslog/accesslog.pb.h"
#include "envoy/config/accesslog/v2/als.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/accesslog/v2/als.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace AccessLog {

// TODO(mattklein123): Stats

/**
 * Interface for an access log streamer. The streamer deals with threading and sends access logs
 * on the correct stream.
 */
class GrpcAccessLogStreamer {
public:
  virtual ~GrpcAccessLogStreamer() {}

  /**
   * Send an access log.
   * @param message supplies the access log to send.
   * @param log_name supplies the name of the log stream to send on.
   */
  virtual void send(envoy::service::accesslog::v2::StreamAccessLogsMessage& message,
                    const std::string& log_name) PURE;
};

typedef std::shared_ptr<GrpcAccessLogStreamer> GrpcAccessLogStreamerSharedPtr;

/**
 * Production implementation of GrpcAccessLogStreamer that supports per-thread and per-log
 * streams.
 */
class GrpcAccessLogStreamerImpl : public Singleton::Instance, public GrpcAccessLogStreamer {
public:
  GrpcAccessLogStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory, ThreadLocal::SlotAllocator& tls,
                            const LocalInfo::LocalInfo& local_info);

  // GrpcAccessLogStreamer
  void send(envoy::service::accesslog::v2::StreamAccessLogsMessage& message,
            const std::string& log_name) override {
    tls_slot_->getTyped<ThreadLocalStreamer>().send(message, log_name);
  }

private:
  /**
   * Shared state that is owned by the per-thread streamers. This allows the main streamer/TLS
   * slot to be destroyed while the streamers hold onto the shared state.
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
  struct ThreadLocalStream : public Grpc::TypedAsyncStreamCallbacks<
                                 envoy::service::accesslog::v2::StreamAccessLogsResponse> {
    ThreadLocalStream(ThreadLocalStreamer& parent, const std::string& log_name)
        : parent_(parent), log_name_(log_name) {}

    // Grpc::TypedAsyncStreamCallbacks
    void onCreateInitialMetadata(Http::HeaderMap&) override {}
    void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override {}
    void onReceiveMessage(
        std::unique_ptr<envoy::service::accesslog::v2::StreamAccessLogsResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

    ThreadLocalStreamer& parent_;
    const std::string log_name_;
    Grpc::AsyncStream* stream_{};
  };

  /**
   * Per-thread multi-stream state.
   */
  struct ThreadLocalStreamer : public ThreadLocal::ThreadLocalObject {
    ThreadLocalStreamer(const SharedStateSharedPtr& shared_state);
    void send(envoy::service::accesslog::v2::StreamAccessLogsMessage& message,
              const std::string& log_name);

    Grpc::AsyncClientPtr client_;
    std::unordered_map<std::string, ThreadLocalStream> stream_map_;
    SharedStateSharedPtr shared_state_;
  };

  ThreadLocal::SlotPtr tls_slot_;
};

/**
 * Access log Instance that streams HTTP logs over gRPC.
 */
class HttpGrpcAccessLog : public Instance {
public:
  HttpGrpcAccessLog(FilterPtr&& filter,
                    const envoy::config::accesslog::v2::HttpGrpcAccessLogConfig& config,
                    GrpcAccessLogStreamerSharedPtr grpc_access_log_streamer);

  static void responseFlagsToAccessLogResponseFlags(
      envoy::api::v2::filter::accesslog::AccessLogCommon& common_access_log,
      const RequestInfo::RequestInfo& request_info);

  // AccessLog::Instance
  void log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
           const RequestInfo::RequestInfo& request_info) override;

private:
  FilterPtr filter_;
  const envoy::config::accesslog::v2::HttpGrpcAccessLogConfig config_;
  GrpcAccessLogStreamerSharedPtr grpc_access_log_streamer_;
};

} // namespace AccessLog
} // namespace Envoy
