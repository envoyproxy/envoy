#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/cleanup.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Router {

class GenericUpstream;
class GenericConnectionPoolCallbacks;
class RouterFilterInterface;
class UpstreamRequest;

// An API for wrapping either an HTTP or a TCP connection pool.
class GenericConnPool : public Logger::Loggable<Logger::Id::router> {
public:
  virtual ~GenericConnPool() = default;

  // Called to create a new HTTP stream or TCP connection. The implementation
  // is then responsible for calling either onPoolReady or onPoolFailure on the
  // supplied GenericConnectionPoolCallbacks.
  virtual void newStream(GenericConnectionPoolCallbacks* callbacks) PURE;
  // Called to cancel a call to newStream. Returns true if a newStream request
  // was canceled, false otherwise.
  virtual bool cancelAnyPendingRequest() PURE;
  // Optionally returns the protocol for the connection pool.
  virtual absl::optional<Http::Protocol> protocol() const PURE;
};

// An API for the UpstreamRequest to get callbacks from either an HTTP or TCP
// connection pool.
class GenericConnectionPoolCallbacks {
public:
  virtual ~GenericConnectionPoolCallbacks() = default;

  virtual void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                             absl::string_view transport_failure_reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;
  virtual void onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                           Upstream::HostDescriptionConstSharedPtr host,
                           const Network::Address::InstanceConstSharedPtr& upstream_local_address,
                           const StreamInfo::StreamInfo& info) PURE;
  virtual UpstreamRequest* upstreamRequest() PURE;
};

// The base request for Upstream.
class UpstreamRequest : public Logger::Loggable<Logger::Id::router>,
                        public Http::ResponseDecoder,
                        public LinkedObject<UpstreamRequest>,
                        public GenericConnectionPoolCallbacks {
public:
  UpstreamRequest(RouterFilterInterface& parent, std::unique_ptr<GenericConnPool>&& conn_pool);
  ~UpstreamRequest() override;

  void encodeHeaders(bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeTrailers(const Http::RequestTrailerMap& trailers);
  void encodeMetadata(Http::MetadataMapPtr&& metadata_map_ptr);

  void resetStream();
  void setupPerTryTimeout();
  void onPerTryTimeout();
  void maybeEndDecode(bool end_stream);
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);

  // Http::StreamDecoder
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeMetadata(Http::MetadataMapPtr&& metadata_map) override;

  // Http::ResponseDecoder
  void decode100ContinueHeaders(Http::ResponseHeaderMapPtr&& headers) override;
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;

  void onResetStream(Http::StreamResetReason reason, absl::string_view transport_failure_reason);

  void disableDataFromDownstreamForFlowControl();
  void enableDataFromDownstreamForFlowControl();

  // GenericConnPool
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(std::unique_ptr<GenericUpstream>&& upstream,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const Network::Address::InstanceConstSharedPtr& upstream_local_address,
                   const StreamInfo::StreamInfo& info) override;
  UpstreamRequest* upstreamRequest() override { return this; }

  void clearRequestEncoder();
  void onStreamMaxDurationReached();

  struct DownstreamWatermarkManager : public Http::DownstreamWatermarkCallbacks {
    DownstreamWatermarkManager(UpstreamRequest& parent) : parent_(parent) {}

    // Http::DownstreamWatermarkCallbacks
    void onBelowWriteBufferLowWatermark() override;
    void onAboveWriteBufferHighWatermark() override;

    UpstreamRequest& parent_;
  };

  void readEnable();
  void encodeBodyAndTrailers();

  // Getters and setters
  Upstream::HostDescriptionConstSharedPtr& upstreamHost() { return upstream_host_; }
  void outlierDetectionTimeoutRecorded(bool recorded) {
    outlier_detection_timeout_recorded_ = recorded;
  }
  bool outlierDetectionTimeoutRecorded() { return outlier_detection_timeout_recorded_; }
  const StreamInfo::UpstreamTiming& upstreamTiming() { return upstream_timing_; }
  void retried(bool value) { retried_ = value; }
  bool retried() { return retried_; }
  bool grpcRqSuccessDeferred() { return grpc_rq_success_deferred_; }
  void grpcRqSuccessDeferred(bool deferred) { grpc_rq_success_deferred_ = deferred; }
  void upstreamCanary(bool value) { upstream_canary_ = value; }
  bool upstreamCanary() { return upstream_canary_; }
  bool awaitingHeaders() { return awaiting_headers_; }
  void recordTimeoutBudget(bool value) { record_timeout_budget_ = value; }
  bool createPerTryTimeoutOnRequestComplete() {
    return create_per_try_timeout_on_request_complete_;
  }
  bool encodeComplete() const { return encode_complete_; }
  RouterFilterInterface& parent() { return parent_; }

private:
  bool shouldSendEndStream() {
    // Only encode end stream if the full request has been received, the body
    // has been sent, and any trailers or metadata have also been sent.
    return encode_complete_ && !buffered_request_body_ && !encode_trailers_ &&
           downstream_metadata_map_vector_.empty();
  }

  RouterFilterInterface& parent_;
  std::unique_ptr<GenericConnPool> conn_pool_;
  bool grpc_rq_success_deferred_;
  Event::TimerPtr per_try_timeout_;
  std::unique_ptr<GenericUpstream> upstream_;
  absl::optional<Http::StreamResetReason> deferred_reset_reason_;
  Buffer::WatermarkBufferPtr buffered_request_body_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  DownstreamWatermarkManager downstream_watermark_manager_{*this};
  Tracing::SpanPtr span_;
  StreamInfo::StreamInfoImpl stream_info_;
  StreamInfo::UpstreamTiming upstream_timing_;
  const MonotonicTime start_time_;
  // Copies of upstream headers/trailers. These are only set if upstream
  // access logging is configured.
  Http::ResponseHeaderMapPtr upstream_headers_;
  Http::ResponseTrailerMapPtr upstream_trailers_;
  Http::MetadataMapVector downstream_metadata_map_vector_;

  // Tracks the number of times the flow of data from downstream has been disabled.
  uint32_t downstream_data_disabled_{};
  bool calling_encode_headers_ : 1;
  bool upstream_canary_ : 1;
  bool decode_complete_ : 1;
  bool encode_complete_ : 1;
  bool encode_trailers_ : 1;
  bool retried_ : 1;
  bool awaiting_headers_ : 1;
  bool outlier_detection_timeout_recorded_ : 1;
  // Tracks whether we deferred a per try timeout because the downstream request
  // had not been completed yet.
  bool create_per_try_timeout_on_request_complete_ : 1;
  // True if the CONNECT headers have been sent but proxying payload is paused
  // waiting for response headers.
  bool paused_for_connect_ : 1;

  // Sentinel to indicate if timeout budget tracking is configured for the cluster,
  // and if so, if the per-try histogram should record a value.
  bool record_timeout_budget_ : 1;

  Event::TimerPtr max_stream_duration_timer_;
};

class HttpConnPool : public GenericConnPool, public Http::ConnectionPool::Callbacks {
public:
  HttpConnPool(Http::ConnectionPool::Instance& conn_pool) : conn_pool_(conn_pool) {}

  // GenericConnPool
  void newStream(GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingRequest() override;
  absl::optional<Http::Protocol> protocol() const override;

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Http::RequestEncoder& callbacks_encoder,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const StreamInfo::StreamInfo& info) override;

private:
  // Points to the actual connection pool to create streams from.
  Http::ConnectionPool::Instance& conn_pool_;
  Http::ConnectionPool::Cancellable* conn_pool_stream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
};

class TcpConnPool : public GenericConnPool, public Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Tcp::ConnectionPool::Instance* conn_pool) : conn_pool_(conn_pool) {}

  void newStream(GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    upstream_handle_ = conn_pool_->newConnection(*this);
  }

  bool cancelAnyPendingRequest() override {
    if (upstream_handle_) {
      upstream_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
      upstream_handle_ = nullptr;
      return true;
    }
    return false;
  }
  absl::optional<Http::Protocol> protocol() const override { return absl::nullopt; }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_handle_ = nullptr;
    callbacks_->onPoolFailure(reason, "", host);
  }

  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Tcp::ConnectionPool::Instance* conn_pool_;
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
};

// A generic API which covers common functionality between HTTP and TCP upstreams.
class GenericUpstream {
public:
  virtual ~GenericUpstream() = default;
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;
  virtual void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) PURE;
  virtual void encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) PURE;
  virtual void encodeTrailers(const Http::RequestTrailerMap& trailers) PURE;
  virtual void readDisable(bool disable) PURE;
  virtual void resetStream() PURE;
};

class HttpUpstream : public GenericUpstream, public Http::StreamCallbacks {
public:
  HttpUpstream(UpstreamRequest& upstream_request, Http::RequestEncoder* encoder)
      : upstream_request_(upstream_request), request_encoder_(encoder) {
    request_encoder_->getStream().addCallbacks(*this);
  }

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override {
    request_encoder_->encodeData(data, end_stream);
  }
  void encodeMetadata(const Http::MetadataMapVector& metadata_map_vector) override {
    request_encoder_->encodeMetadata(metadata_map_vector);
  }
  void encodeHeaders(const Http::RequestHeaderMap& headers, bool end_stream) override {
    request_encoder_->encodeHeaders(headers, end_stream);
  }
  void encodeTrailers(const Http::RequestTrailerMap& trailers) override {
    request_encoder_->encodeTrailers(trailers);
  }

  void readDisable(bool disable) override { request_encoder_->getStream().readDisable(disable); }

  void resetStream() override {
    request_encoder_->getStream().removeCallbacks(*this);
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }

  // Http::StreamCallbacks
  void onResetStream(Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override {
    upstream_request_.onResetStream(reason, transport_failure_reason);
  }

  void onAboveWriteBufferHighWatermark() override {
    upstream_request_.disableDataFromDownstreamForFlowControl();
  }

  void onBelowWriteBufferLowWatermark() override {
    upstream_request_.enableDataFromDownstreamForFlowControl();
  }

private:
  UpstreamRequest& upstream_request_;
  Http::RequestEncoder* request_encoder_{};
};

class TcpUpstream : public GenericUpstream, public Tcp::ConnectionPool::UpstreamCallbacks {
public:
  TcpUpstream(UpstreamRequest* upstream_request, Tcp::ConnectionPool::ConnectionDataPtr&& upstream);

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Http::MetadataMapVector&) override {}
  void encodeHeaders(const Http::RequestHeaderMap&, bool end_stream) override;
  void encodeTrailers(const Http::RequestTrailerMap&) override;
  void readDisable(bool disable) override;
  void resetStream() override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

private:
  UpstreamRequest* upstream_request_;
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

} // namespace Router
} // namespace Envoy
