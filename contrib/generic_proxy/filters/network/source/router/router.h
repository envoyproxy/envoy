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
#include "contrib/generic_proxy/filters/network/source/upstream.h"

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
};

class RouterFilter;
class UpstreamRequest;

class GenericUpstream : public UpstreamConnection, public ClientCodecCallbacks {
public:
  GenericUpstream(Upstream::TcpPoolData&& tcp_pool_data, ClientCodecPtr&& client_codec)
      : UpstreamConnection(std::move(tcp_pool_data), std::move(client_codec)) {
    client_codec_->setCodecCallbacks(*this);
  }

  // ResponseDecoderCallback
  void writeToConnection(Buffer::Instance& buffer) override;
  OptRef<Network::Connection> connection() override;

  virtual void insertUpstreamRequest(uint64_t stream_id, UpstreamRequest* pending_request) PURE;
  virtual void removeUpstreamRequest(uint64_t stream_id) PURE;
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

  // ResponseDecoderCallback
  void onDecodingSuccess(StreamFramePtr response) override;
  void onDecodingFailure() override;

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

  // ResponseDecoderCallback
  void onDecodingSuccess(StreamFramePtr response) override;
  void onDecodingFailure() override;

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
  void resetStream(StreamResetReason reason);
  void clearStream(bool close_connection);

  // Called when the stream has been reset or completed.
  void deferredDelete();

  void onUpstreamFailure(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason,
                         Upstream::HostDescriptionConstSharedPtr host);
  void onUpstreamSuccess(Upstream::HostDescriptionConstSharedPtr host);

  void onConnectionClose(Network::ConnectionEvent event);

  void onDecodingSuccess(StreamFramePtr response);
  void onDecodingFailure();

  // RequestEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void encodeBufferToUpstream(Buffer::Instance& buffer);

  void sendRequestStartToUpstream();
  void sendRequestFrameToUpstream();

  RouterFilter& parent_;
  uint64_t stream_id_{};

  GenericUpstreamSharedPtr generic_upstream_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;

  Buffer::OwnedImpl upstream_request_buffer_;

  StreamInfo::StreamInfoImpl stream_info_;
  OptRef<const Tracing::Config> tracing_config_;
  Tracing::SpanPtr span_;

  // One of these flags should be set to true when the request is complete.
  bool stream_reset_{};
  bool response_complete_{};

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
                     public StreamFrameHandler,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouterFilter(RouterConfigSharedPtr config, Server::Configuration::FactoryContext& context)
      : config_(std::move(config)), context_(context) {}

  // DecoderFilter
  void onDestroy() override;

  void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) override {
    callbacks_ = &callbacks;
    // Set handler for following request frames.
    callbacks_->setRequestFramesHandler(*this);
  }
  FilterStatus onStreamDecoded(StreamRequest& request) override;

  void onResponseStart(StreamResponsePtr response);
  void onResponseFrame(StreamFramePtr frame);
  void completeDirectly();

  void onUpstreamRequestReset(UpstreamRequest& upstream_request, StreamResetReason reason);
  void cleanUpstreamRequests(bool filter_complete);

  void setRouteEntry(const RouteEntry* route_entry) { route_entry_ = route_entry; }

  std::list<UpstreamRequestPtr>& upstreamRequestsForTest() { return upstream_requests_; }

  // Upstream::LoadBalancerContextBase
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override;
  const Network::Connection* downstreamConnection() const override;

  // StreamFrameHandler
  void onStreamFrame(StreamFramePtr frame) override;

private:
  friend class UpstreamRequest;
  friend class UpstreamManagerImpl;

  void kickOffNewUpstreamRequest();
  void resetStream(StreamResetReason reason);

  // Set filter_complete_ to true before any local or upstream response. Because the
  // response processing may complete and destroy the L7 filter chain directly and cause the
  // onDestory() of RouterFilter to be called. The filter_complete_ will be used to block
  // unnecessary clearUpstreamRequests() in the onDestory() of RouterFilter.
  bool filter_complete_{};

  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  Request* request_stream_{};
  std::list<StreamFramePtr> request_stream_frames_;
  bool request_stream_end_{};

  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_;

  std::list<UpstreamRequestPtr> upstream_requests_;

  DecoderFilterCallback* callbacks_{};

  RouterConfigSharedPtr config_;
  Server::Configuration::FactoryContext& context_;
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
