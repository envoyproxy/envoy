#pragma once

#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_impl.h"

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

class UpstreamConnectionManager : public UpstreamConnectionManagerBase {
public:
  UpstreamConnectionManager(UpstreamRequest& parent, Upstream::TcpPoolData&& tcp_data,
                            ResponseDecoderCallback& cb);

  void onEventImpl(Network::ConnectionEvent event) override;
  void onPoolSuccessImpl() override;
  void onPoolFailureImpl(ConnectionPool::PoolFailureReason reason,
                         absl::string_view transport_failure_reason) override;

  UpstreamRequest& parent_;
};

class UpstreamRequest : public UpstreamBindingCallback,
                        public LinkedObject<UpstreamRequest>,
                        public Envoy::Event::DeferredDeletable,
                        public RequestEncoderCallback,
                        public PendingResponseCallback,
                        Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  UpstreamRequest(RouterFilter& parent, absl::optional<Upstream::TcpPoolData> tcp_data);

  void startStream();
  void resetStream(StreamResetReason reason);
  void completeUpstreamRequest(bool close_connection);

  // Called when the stream has been reset or completed.
  void deferredDelete();

  // UpstreamBindingCallback
  void onBindFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onBindSuccess(Tcp::ConnectionPool::ConnectionData& conn,
                     Upstream::HostDescriptionConstSharedPtr host) override;

  // PendingResponseCallback
  void onDecodingSuccess(ResponsePtr response, ExtendedOptions options) override;
  void onDecodingFailure() override;
  void onConnectionClose(Network::ConnectionEvent event) override;

  // RequestEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer) override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void encodeBufferToUpstream(Buffer::Instance& buffer);

  bool stream_reset_{};

  RouterFilter& parent_;

  absl::optional<Upstream::TcpPoolData> pool_data_;
  std::unique_ptr<UpstreamConnectionManager> upstream_connection_manager_;

  Tcp::ConnectionPool::ConnectionData* conn_data_{};
  Upstream::HostDescriptionConstSharedPtr upstream_host_;

  bool response_complete_{};
  ResponseDecoderPtr response_decoder_;

  Buffer::OwnedImpl upstream_request_buffer_;
  bool expect_response_{};

  StreamInfo::StreamInfoImpl stream_info_;

  OptRef<const Tracing::Config> tracing_config_;
  Tracing::SpanPtr span_;
};
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

class RouterFilter : public DecoderFilter,
                     public Upstream::LoadBalancerContextBase,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouterFilter(Server::Configuration::FactoryContext& context) : context_(context) {}

  // DecoderFilter
  void onDestroy() override;

  void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) override {
    callbacks_ = &callbacks;
    protocol_options_ = callbacks_->downstreamCodec().protocolOptions();
  }
  FilterStatus onStreamDecoded(Request& request) override;

  void onUpstreamResponse(ResponsePtr response, ExtendedOptions options);
  void completeDirectly();

  void onUpstreamRequestReset(UpstreamRequest& upstream_request, StreamResetReason reason);
  void cleanUpstreamRequests(bool filter_complete);

  void setRouteEntry(const RouteEntry* route_entry) { route_entry_ = route_entry; }

  std::list<UpstreamRequestPtr>& upstreamRequestsForTest() { return upstream_requests_; }

private:
  friend class UpstreamRequest;
  friend class UpstreamConnectionManager;

  void kickOffNewUpstreamRequest();
  void resetStream(StreamResetReason reason);

  // Set filter_complete_ to true before any local or upstream response. Because the
  // response processing may complete and destroy the L7 filter chain directly and cause the
  // onDestory() of RouterFilter to be called. The filter_complete_ will be used to block
  // unnecessary clearUpstreamRequests() in the onDestory() of RouterFilter.
  bool filter_complete_{};

  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  Request* request_{};

  RequestEncoderPtr request_encoder_;

  std::list<UpstreamRequestPtr> upstream_requests_;

  DecoderFilterCallback* callbacks_{};
  ProtocolOptions protocol_options_;

  Server::Configuration::FactoryContext& context_;
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
