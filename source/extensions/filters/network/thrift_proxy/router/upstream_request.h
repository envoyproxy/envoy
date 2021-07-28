#pragma once

#include "envoy/common/time.h"
#include "envoy/tcp/conn_pool.h"

#include "source/extensions/filters/network/thrift_proxy/decoder_events.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

struct UpstreamRequest : public Tcp::ConnectionPool::Callbacks {
  UpstreamRequest(RequestOwner& parent, Upstream::TcpPoolData& pool_data,
                  MessageMetadataSharedPtr& metadata, TransportType transport_type,
                  ProtocolType protocol_type);
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

  void onRequestStart(bool continue_decoding);
  void onRequestComplete();
  void onResponseComplete();
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void onResetStream(ConnectionPool::PoolFailureReason reason);
  void chargeResponseTiming();

  RequestOwner& parent_;
  Upstream::TcpPoolData& conn_pool_data_;
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
  Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  ThriftConnectionState* conn_state_{};
  TransportPtr transport_;
  ProtocolPtr protocol_;
  ThriftObjectPtr upgrade_response_;

  bool request_complete_ : 1;
  bool response_started_ : 1;
  bool response_complete_ : 1;

  bool charged_response_timing_{false};
  MonotonicTime downstream_request_complete_time_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
