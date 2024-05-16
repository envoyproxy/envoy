#pragma once

#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/access_loggers/common/grpc_access_logger_utils.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

template <typename LogRequest, typename LogResponse> class GrpcAccessLogClient {
public:
  virtual ~GrpcAccessLogClient() = default;
  virtual bool isConnected() PURE;
  virtual bool log(const LogRequest& request) PURE;

protected:
  GrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                      const Protobuf::MethodDescriptor& service_method,
                      OptRef<const envoy::config::core::v3::RetryPolicy> retry_policy)
      : client_(client), service_method_(service_method),
        opts_(createRequestOptionsForRetry(retry_policy)) {}

  Grpc::AsyncClient<LogRequest, LogResponse> client_;
  const Protobuf::MethodDescriptor& service_method_;
  const Http::AsyncClient::RequestOptions opts_;

private:
  Http::AsyncClient::RequestOptions
  createRequestOptionsForRetry(OptRef<const envoy::config::core::v3::RetryPolicy> retry_policy) {
    auto opt = Http::AsyncClient::RequestOptions();

    if (!retry_policy) {
      return opt;
    }

    const auto grpc_retry_policy =
        Http::Utility::convertCoreToRouteRetryPolicy(*retry_policy, "connect-failure");
    opt.setBufferBodyForRetry(true);
    opt.setRetryPolicy(grpc_retry_policy);
    return opt;
  }
};

template <typename LogRequest, typename LogResponse>
class UnaryGrpcAccessLogClient : public GrpcAccessLogClient<LogRequest, LogResponse> {
public:
  typedef std::function<Grpc::AsyncRequestCallbacks<LogResponse>&()> AsyncRequestCallbacksFactory;

  UnaryGrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                           const Protobuf::MethodDescriptor& service_method,
                           OptRef<const envoy::config::core::v3::RetryPolicy> retry_policy,
                           AsyncRequestCallbacksFactory callback_factory)
      : GrpcAccessLogClient<LogRequest, LogResponse>(client, service_method, retry_policy),
        callbacks_factory_(callback_factory) {}

  bool isConnected() override { return false; }

  bool log(const LogRequest& request) override {
    GrpcAccessLogClient<LogRequest, LogResponse>::client_->send(
        GrpcAccessLogClient<LogRequest, LogResponse>::service_method_, request,
        callbacks_factory_(), Tracing::NullSpan::instance(),
        GrpcAccessLogClient<LogRequest, LogResponse>::opts_);
    return true;
  }

private:
  AsyncRequestCallbacksFactory callbacks_factory_;
};

template <typename LogRequest, typename LogResponse>
class StreamingGrpcAccessLogClient : public GrpcAccessLogClient<LogRequest, LogResponse> {
public:
  StreamingGrpcAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                               const Protobuf::MethodDescriptor& service_method,
                               OptRef<const envoy::config::core::v3::RetryPolicy> retry_policy)
      : GrpcAccessLogClient<LogRequest, LogResponse>(client, service_method, retry_policy) {}

public:
  struct LocalStream : public Grpc::AsyncStreamCallbacks<LogResponse> {
    LocalStream(StreamingGrpcAccessLogClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<LogResponse>&&) override {}
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.stream_ != nullptr);
      if (parent_.stream_->stream_ != nullptr) {
        // Only reset if we have a stream. Otherwise we had an inline failure and we will clear the
        // stream data in send().
        parent_.stream_.reset();
      }
    }

    StreamingGrpcAccessLogClient& parent_;
    Grpc::AsyncStream<LogRequest> stream_{};
  };

  bool isConnected() override { return stream_ != nullptr && stream_->stream_ != nullptr; }

  bool log(const LogRequest& request) override {
    if (!stream_) {
      stream_ = std::make_unique<LocalStream>(*this);
    }

    if (stream_->stream_ == nullptr) {
      stream_->stream_ = GrpcAccessLogClient<LogRequest, LogResponse>::client_->start(
          GrpcAccessLogClient<LogRequest, LogResponse>::service_method_, *stream_,
          GrpcAccessLogClient<LogRequest, LogResponse>::opts_);
    }

    if (stream_->stream_ != nullptr) {
      if (stream_->stream_->isAboveWriteBufferHighWatermark()) {
        return false;
      }
      stream_->stream_->sendMessage(request, false);
    } else {
      // Clear out the stream data due to stream creation failure.
      stream_.reset();
    }
    return true;
  }

  std::unique_ptr<LocalStream> stream_;
};

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
