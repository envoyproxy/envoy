#pragma once

#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/upstream/load_balancer_impl.h"

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
};

class RouterFilter;

class UpstreamRequest : public Tcp::ConnectionPool::Callbacks,
                        public Tcp::ConnectionPool::UpstreamCallbacks,
                        public LinkedObject<UpstreamRequest>,
                        public Envoy::Event::DeferredDeletable,
                        public ResponseDecoderCallback,
                        Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  UpstreamRequest(RouterFilter& parent, Upstream::TcpPoolData tcp_data);

  void startStream();
  void resetStream(StreamResetReason reason);

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // ResponseDecoderCallback
  void onDecodingSuccess(ResponsePtr response) override;
  void onDecodingFailure() override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void encodeBufferToUpstream(Buffer::Instance& buffer);

  void completeUpstreamRequest();

  bool stream_reset_{};

  RouterFilter& parent_;
  Upstream::TcpPoolData tcp_data_;

  Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;

  bool request_complete_{};
  bool response_started_{};
  bool response_complete_{};
  ResponseDecoderPtr response_decoder_;
};
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

class RouterFilter : public DecoderFilter,
                     public Upstream::LoadBalancerContextBase,
                     public RequestEncoderCallback,
                     Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  RouterFilter(Server::Configuration::FactoryContext& context) : context_(context) {}

  // DecoderFilter
  void onDestroy() override;

  void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) override {
    callbacks_ = &callbacks;
  }
  FilterStatus onStreamDecoded(Request& request) override;

  // RequestEncoderCallback
  void onEncodingSuccess(Buffer::Instance& buffer, bool expect_response) override;

  void onUpstreamResponse(ResponsePtr response);
  void completeDirectly();

  void onUpstreamRequestReset(UpstreamRequest& upstream_request, StreamResetReason reason);
  void cleanUpstreamRequests(bool filter_complete);

  void setRouteEntry(const RouteEntry* route_entry) { route_entry_ = route_entry; }

  std::list<UpstreamRequestPtr>& upstreamRequestsForTest() { return upstream_requests_; }

private:
  friend class UpstreamRequest;

  void kickOffNewUpstreamRequest();
  void resetStream(StreamResetReason reason);

  bool expect_response_{};
  bool filter_complete_{};

  const RouteEntry* route_entry_{};

  Buffer::OwnedImpl upstream_request_buffer_;

  RequestEncoderPtr request_encoder_;

  std::list<UpstreamRequestPtr> upstream_requests_;

  DecoderFilterCallback* callbacks_{};

  Server::Configuration::FactoryContext& context_;
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
