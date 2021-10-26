#pragma once

#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/upstream/load_balancer_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_stream.h"

namespace Envoy {
namespace Proxy {
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
                        public Event::DeferredDeletable,
                        public ResponseDecoderCallback,
                        Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  UpstreamRequest(RouterFilter& parent, Tcp::ConnectionPool::Instance& pool);

  void startStream();
  void resetStream(StreamResetReason reason);

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // ResponseDecoderCallback
  void onGenericResponse(GenericResponsePtr response) override;
  void onDecodingError() override;

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void encodeBufferToUpstream(Buffer::Instance& buffer);

  bool stream_reset_{};

  RouterFilter& parent_;
  Tcp::ConnectionPool::Instance& conn_pool_;

  Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;

  bool response_started_{};
  bool response_complete_{};
  GenericResponseDecoderPtr response_decoder_;
};
using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

class RouterFilter : public DecoderFilter,
                     Logger::Loggable<Envoy::Logger::Id::filter>,
                     public Upstream::LoadBalancerContextBase {
public:
  RouterFilter(Server::Configuration::FactoryContext& context) : context_(context) {}

  // DecoderFilter
  void onDestroy() override;

  GenericFilterStatus onStreamDecoded(GenericRequest& request) override;
  void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) override {
    callbacks_ = &callbacks;
  }

  void onUpstreamResponse(GenericResponsePtr response);
  void resetStream(StreamResetReason reason);

  void onUpstreamRequestReset(UpstreamRequest& upstream_request, StreamResetReason reason);
  void cleanUpstreamRequests(bool filter_complete);

private:
  friend class UpstreamRequest;

  bool filter_complete_{};

  Buffer::OwnedImpl upstream_request_buffer_;
  GenericRequestEncoderPtr request_encoder_;

  std::list<UpstreamRequestPtr> upstream_requests_;

  DecoderFilterCallback* callbacks_{};

  Server::Configuration::FactoryContext& context_;
};

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
