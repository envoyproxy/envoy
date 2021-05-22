#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/router/router.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/upstream/load_balancer_impl.h"

#include "extensions/filters/network/thrift_proxy/conn_manager.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

struct NullResponseDecoder : public DecoderCallbacks, public ProtocolConverter {
  NullResponseDecoder(Transport& transport, Protocol& protocol)
      : decoder_(std::make_unique<Decoder>(transport, protocol, *this)) {
    initProtocolConverter(protocol, response_buffer_);
  }

  bool onData(Buffer::Instance& data);

  // ProtocolConverter
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus messageEnd() override;
  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override;
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
    UNREFERENCED_PARAMETER(metadata);
    return FilterStatus::Continue;
  }
  FilterStatus transportEnd() override;

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler() override { return *this; }
  bool passthroughEnabled() const override { return false; }

  DecoderPtr decoder_;
  Buffer::OwnedImpl response_buffer_;
  Buffer::OwnedImpl upstream_buffer_;
  MessageMetadataSharedPtr metadata_;
  bool complete_ : 1;
};
using NullResponseDecoderPtr = std::unique_ptr<NullResponseDecoder>;

class ShadowWriterImpl;

struct ShadowRequest : public ShadowRequestHandle,
                       public Tcp::ConnectionPool::Callbacks,
                       public Tcp::ConnectionPool::UpstreamCallbacks,
                       public Event::DeferredDeletable,
                       public LinkedObject<ShadowRequest>,
                       Logger::Loggable<Logger::Id::thrift> {
public:
  ShadowRequest(ShadowWriterImpl& parent_, Tcp::ConnectionPool::Instance& pool,
                MessageMetadataSharedPtr& metadata, TransportType transport_type,
                ProtocolType protocol_type);
  ~ShadowRequest() override;

  void start();
  void resetStream();
  void cleanup();
  void maybeCleanup();

  // Router::ShadowRequestHandle
  void tryWriteRequest(const Buffer::OwnedImpl& buffer) override;
  void tryReleaseConnection() override;

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  bool requestInProgress();
  void releaseConnection(bool close);
  void onResetStream(ConnectionPool::PoolFailureReason reason);

  ShadowWriterImpl& parent_;
  Tcp::ConnectionPool::Instance& conn_pool_;
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  ThriftConnectionState* conn_state_{};
  TransportPtr transport_;
  ProtocolPtr protocol_;
  ThriftObjectPtr upgrade_response_;
  bool original_request_done_{};
  bool request_sent_{};
  Buffer::OwnedImpl request_buffer_;
  NullResponseDecoderPtr response_decoder_;
};

class ShadowWriterImpl : public ShadowWriter,
                         public Upstream::LoadBalancerContextBase,
                         Logger::Loggable<Logger::Id::thrift> {
public:
  ShadowWriterImpl(Upstream::ClusterManager& cm, Event::Dispatcher&) : cm_(cm) {}

  ~ShadowWriterImpl() {
    while (!active_requests_.empty()) {
      active_requests_.front()->resetStream();
      active_requests_.front()->cleanup();
    }
  }

  // Router::ShadowWriter
  absl::optional<std::reference_wrapper<ShadowRequestHandle>>
  submit(const std::string& cluster_name, MessageMetadataSharedPtr metadata,
         TransportType original_transport, ProtocolType original_protocol) override;

  // Upstream::LoadBalancerContext
  const Network::Connection* downstreamConnection() const override { return nullptr; }
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }

private:
  friend struct ShadowRequest;

  Upstream::ClusterManager& cm_;
  // Event::Dispatcher& dispatcher_;
  std::list<std::unique_ptr<ShadowRequest>> active_requests_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
