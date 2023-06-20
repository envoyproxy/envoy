#pragma once

#include "envoy/common/time.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/thrift_proxy/decoder_events.h"
#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class UpstreamResponseCallbacks {
public:
  virtual ~UpstreamResponseCallbacks() = default;

  virtual void startUpstreamResponse(Transport& transport, Protocol& protocol) PURE;
  virtual ThriftFilters::ResponseStatus upstreamData(Buffer::Instance& buffer) PURE;
  virtual MessageMetadataSharedPtr responseMetadata() PURE;
  virtual bool responseSuccess() PURE;
};

struct UpstreamRequest : public Tcp::ConnectionPool::Callbacks,
                         Logger::Loggable<Logger::Id::thrift> {
  UpstreamRequest(RequestOwner& parent, Upstream::TcpPoolData& pool_data,
                  MessageMetadataSharedPtr& metadata, TransportType transport_type,
                  ProtocolType protocol_type, bool close_downstream_on_error);
  ~UpstreamRequest() override;

  FilterStatus start();
  void resetStream();
  void releaseConnection(bool close);

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  bool handleUpstreamData(Buffer::Instance& data, bool end_stream,
                          UpstreamResponseCallbacks& callbacks);
  void handleUpgradeResponse(Buffer::Instance& data);
  ThriftFilters::ResponseStatus handleRegularResponse(Buffer::Instance& data,
                                                      UpstreamResponseCallbacks& callbacks);
  uint64_t encodeAndWrite(Buffer::OwnedImpl& request_buffer);
  void onEvent(Network::ConnectionEvent event);
  void onRequestStart(bool continue_decoding);
  void onRequestComplete();
  void onResponseComplete();
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  bool onResetStream(ConnectionPool::PoolFailureReason reason);
  void chargeResponseTiming();

  RequestOwner& parent_;
  const RouterStats& stats_;
  Upstream::TcpPoolData& conn_pool_data_;
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  ThriftConnectionState* conn_state_{};
  TransportPtr transport_;
  ProtocolPtr protocol_;
  ThriftObjectPtr upgrade_response_;

  enum class ResponseState : uint8_t {
    None = 0,
    Started = 1,
    Completed = 2,
    ConnectionReleased = 3
  };

  ResponseState response_state_{ResponseState::None};
  bool request_complete_ : 1;
  bool response_underflow_ : 1;
  bool charged_response_timing_ : 1;
  bool close_downstream_on_error_ : 1;

  absl::optional<MonotonicTime> downstream_request_complete_time_;
  uint64_t response_size_{};
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
