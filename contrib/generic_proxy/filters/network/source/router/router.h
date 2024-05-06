#pragma once

#include <cstdint>

#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_impl.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/router/v3/router.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/router/v3/router.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

/**
 * Stream reset reasons.
 */
enum class StreamResetReason : uint32_t {
  LocalReset,
  // If the stream was locally reset by a connection pool due to an initial connection failure.
  ConnectionFailure,
  // If the stream was locally reset due to connection termination.
  ConnectionTermination,
  // The stream was reset because of a resource overflow.
  Overflow,
  // Protocol error.
  ProtocolError,

  LastReason = ProtocolError,
};

class RouterFilter;
class UpstreamRequest;

class GenericUpstream : public ClientCodecCallbacks,
                        public Envoy::Event::DeferredDeletable,
                        public Tcp::ConnectionPool::Callbacks,
                        public Tcp::ConnectionPool::UpstreamCallbacks,
                        public Envoy::Logger::Loggable<Envoy::Logger::Id::upstream> {
public:
  GenericUpstream(Upstream::TcpPoolData&& tcp_pool_data, ClientCodecPtr&& client_codec)
      : tcp_pool_data_(std::move(tcp_pool_data)), client_codec_(std::move(client_codec)),
        upstream_host_(tcp_pool_data_.host()) {
    client_codec_->setCodecCallbacks(*this);
  }
  ~GenericUpstream() override;

  void initialize();
  virtual void cleanUp(bool close_connection);

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason, absl::string_view,
                     Upstream::HostDescriptionConstSharedPtr) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override { onEventImpl(event); }

  ClientCodec& clientCodec() { return *client_codec_; }
  void mayUpdateUpstreamHost(Upstream::HostDescriptionConstSharedPtr real_host) {
    if (real_host == nullptr || real_host == upstream_host_) {
      return;
    }

    // Update the upstream host iff the connection pool callbacks provide a different
    // value.
    upstream_host_ = std::move(real_host);
  }
  Upstream::HostDescriptionConstSharedPtr upstreamHost() const { return upstream_host_; }

  // ResponseDecoderCallback
  void writeToConnection(Buffer::Instance& buffer) override;
  OptRef<Network::Connection> connection() override;
  OptRef<const Upstream::ClusterInfo> upstreamCluster() const override {
    ASSERT(upstream_host_ != nullptr);
    return upstream_host_->cluster();
  }

  virtual void onEventImpl(Network::ConnectionEvent event) PURE;
  virtual void onPoolSuccessImpl() PURE;
  virtual void onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                                 absl::string_view transport_failure_reason) PURE;
  virtual void insertUpstreamRequest(uint64_t stream_id, UpstreamRequest* pending_request) PURE;
  virtual void removeUpstreamRequest(uint64_t stream_id) PURE;

protected:
  Upstream::TcpPoolData tcp_pool_data_;
  ClientCodecPtr client_codec_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;

  // Whether the upstream connection is created. This will be set to true when the initialize()
  // is called.
  bool initialized_{};

  Tcp::ConnectionPool::Cancellable* tcp_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr owned_conn_data_;
};
using GenericUpstreamSharedPtr = std::shared_ptr<GenericUpstream>;

class BoundGenericUpstream : public GenericUpstream,
                             public StreamInfo::FilterState::Object,
                             public std::enable_shared_from_this<BoundGenericUpstream> {
public:
  BoundGenericUpstream(const CodecFactory& codec_factory,
                       Envoy::Upstream::TcpPoolData&& tcp_pool_data,
                       Network::Connection& downstream_connection);

  void onDownstreamConnectionEvent(Network::ConnectionEvent event);

  // UpstreamConnection
  void onPoolSuccessImpl() override;
  void onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;
  void onEventImpl(Network::ConnectionEvent event) override;
  void cleanUp(bool close_connection) override;

  // ClientCodecCallbacks
  void onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                         absl::optional<StartTime> start_time = {}) override;
  void onDecodingSuccess(ResponseCommonFramePtr response_common_frame) override;
  void onDecodingFailure(absl::string_view reason = {}) override;

  // GenericUpstream
  void insertUpstreamRequest(uint64_t stream_id, UpstreamRequest* pending_request) override;
  void removeUpstreamRequest(uint64_t stream_id) override;

  const auto& waitingResponseRequestsForTest() const { return waiting_response_requests_; }
  const auto& waitingUpstreamRequestsForTest() const { return waiting_upstream_requests_; }

private:
  struct EventWatcher : public Network::ConnectionCallbacks {
    EventWatcher(BoundGenericUpstream& parent) : parent_(parent) {}
    BoundGenericUpstream& parent_;

    // Network::ConnectionCallbacks
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}
    void onEvent(Network::ConnectionEvent downstream_event) override {
      parent_.onDownstreamConnectionEvent(downstream_event);
    }
  };

  Network::Connection& downstream_connection_;

  std::unique_ptr<EventWatcher> connection_event_watcher_;

  absl::optional<bool> upstream_connection_ready_;
  bool prevent_clean_up_{};

  // Pending upstream requests that are waiting for the upstream response to be received.
  absl::flat_hash_map<uint64_t, UpstreamRequest*> waiting_response_requests_;
  // Pending upstream requests that are waiting for the upstream connection to be ready.
  absl::flat_hash_map<uint64_t, UpstreamRequest*> waiting_upstream_requests_;
};

class OwnedGenericUpstream : public GenericUpstream {
public:
  OwnedGenericUpstream(const CodecFactory& codec_factory,
                       Envoy::Upstream::TcpPoolData&& tcp_pool_data);

  void setResponseCallback();

  // UpstreamConnection
  void onEventImpl(Network::ConnectionEvent event) override;
  void onPoolSuccessImpl() override;
  void onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;

  // ClientCodecCallbacks
  void onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                         absl::optional<StartTime> start_time = {}) override;
  void onDecodingSuccess(ResponseCommonFramePtr response_common_frame) override;
  void onDecodingFailure(absl::string_view reason = {}) override;

  // GenericUpstream
  void insertUpstreamRequest(uint64_t stream_id, UpstreamRequest* pending_request) override;
  void removeUpstreamRequest(uint64_t) override {}

private:
  UpstreamRequest* upstream_request_{};
};

class UpstreamRequest : public LinkedObject<UpstreamRequest>,
                        public Envoy::Event::DeferredDeletable,
                        public EncodingCallbacks,
                        Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  UpstreamRequest(RouterFilter& parent, GenericUpstreamSharedPtr generic_upstream);

  void startStream();
  void resetStream(StreamResetReason reason, absl::string_view reason_detail);
  void clearStream(bool close_connection);

  // Called when the stream has been reset or completed.
  void deferredDelete();

  void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason);
  void onUpstreamSuccess();

  void onConnectionClose(Network::ConnectionEvent event);

  void onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                         absl::optional<StartTime> start_time = {});
  void onDecodingSuccess(ResponseCommonFramePtr response_common_frame);
  void onDecodingFailure(absl::string_view reason);

  // RequestEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) override;
  void onEncodingFailure(absl::string_view reason) override;
  OptRef<const RouteEntry> routeEntry() const override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void onUpstreamConnectionReady();
  void encodeBufferToUpstream(Buffer::Instance& buffer);

  void sendRequestStartToUpstream();
  void sendRequestFrameToUpstream();

  void onUpstreamResponseComplete(bool drain_close);

  RouterFilter& parent_;
  uint64_t stream_id_{};

  GenericUpstreamSharedPtr generic_upstream_;

  Buffer::OwnedImpl upstream_request_buffer_;

  StreamInfo::StreamInfoImpl stream_info_;
  std::shared_ptr<StreamInfo::UpstreamInfoImpl> upstream_info_;
  OptRef<const Tracing::Config> tracing_config_;
  Tracing::SpanPtr span_;

  absl::optional<MonotonicTime> connecting_start_time_;

  // One of these flags should be set to true when the request is complete.
  bool reset_or_response_complete_{};

  bool expects_response_{};

  bool request_stream_header_sent_{};
  bool response_stream_header_received_{};
};
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

class RouterConfig {
public:
  RouterConfig(const envoy::extensions::filters::network::generic_proxy::router::v3::Router& config)
      : bind_upstream_connection_(config.bind_upstream_connection()) {}

  bool bindUpstreamConnection() const { return bind_upstream_connection_; }

private:
  const bool bind_upstream_connection_{};
};
using RouterConfigSharedPtr = std::shared_ptr<RouterConfig>;

class RouterFilter : public DecoderFilter,
                     public Upstream::LoadBalancerContextBase,
                     public RequestFramesHandler,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouterFilter(RouterConfigSharedPtr config, Server::Configuration::FactoryContext& context)
      : config_(std::move(config)),
        cluster_manager_(context.serverFactoryContext().clusterManager()),
        time_source_(context.serverFactoryContext().timeSource()) {}

  // DecoderFilter
  void onDestroy() override;

  void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) override {
    callbacks_ = &callbacks;
    // Set handler for following request frames.
    callbacks_->setRequestFramesHandler(*this);
  }
  FilterStatus onStreamDecoded(StreamRequest& request) override;

  void onResponseStart(ResponseHeaderFramePtr response_header_frame);
  void onResponseFrame(ResponseCommonFramePtr response_common_frame);
  void completeDirectly();

  void onUpstreamRequestReset(UpstreamRequest& upstream_request, StreamResetReason reason,
                              absl::string_view reason_detail);
  void onTimeout();

  void setRouteEntry(const RouteEntry* route_entry) {
    route_entry_ = route_entry;
    max_retries_ = route_entry_ ? route_entry->retryPolicy().numRetries() : 1;
  }

  std::list<UpstreamRequestPtr>& upstreamRequestsForTest() { return upstream_requests_; }

  // Upstream::LoadBalancerContextBase
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override;
  const Network::Connection* downstreamConnection() const override;

  // RequestFramesHandler
  void onRequestCommonFrame(RequestCommonFramePtr frame) override;

private:
  friend class UpstreamRequest;
  friend class UpstreamManagerImpl;

  void kickOffNewUpstreamRequest();
  void resetStream(StreamResetReason reason, absl::string_view reason_detail);
  void completeAndSendLocalReply(absl::Status status, absl::string_view details,
                                 absl::optional<StreamInfo::CoreResponseFlag> flag = {});

  // Clean up all the upstream requests, statuses and timers. All further events will be
  // ignored.
  void onFilterComplete();

  // Check if all request frames are ready and fire the timeout timer if necessary.
  void mayRequestStreamEnd(bool stream_end_stream);

  // Check if retry is allowed.
  bool couldRetry(absl::optional<StreamResetReason> reason) {
    // If the upstream connection is bound to the downstream connection and resetting happens,
    // we should not retry the request because the downstream connection will be closed.
    if (reason.has_value() && config_->bindUpstreamConnection()) {
      return false;
    }
    return num_retries_ < max_retries_;
  }

  // Set this flag if the downstream request is cancelled, reset or completed, or the upstream
  // response is completely received to tell the filter to ignore all further events.
  bool filter_complete_{};

  const RouteEntry* route_entry_{};
  uint32_t num_retries_{0};
  uint32_t max_retries_{1};

  Upstream::ClusterInfoConstSharedPtr cluster_;
  Request* request_stream_{};
  std::list<RequestCommonFramePtr> request_stream_frames_;
  bool request_stream_end_{};

  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_;

  std::list<UpstreamRequestPtr> upstream_requests_;
  Envoy::Event::TimerPtr timeout_timer_;

  DecoderFilterCallback* callbacks_{};

  RouterConfigSharedPtr config_;
  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
