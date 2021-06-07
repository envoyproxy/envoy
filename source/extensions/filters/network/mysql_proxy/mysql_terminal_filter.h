#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * Implementation of MySQL proxy filter.
 */
class MySQLTerminalFilter : public Tcp::ConnectionPool::Callbacks,
                            public MySQLMoniterFilter,
                            public Network::ConnectionCallbacks {
public:
  MySQLTerminalFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
                      DecoderFactory& factory);
  ~MySQLTerminalFilter() override = default;
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Tcp::ConnectionPool::Callbacks
  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolFailure(Tcp::ConnectionPool::PoolFailureReason,
                     Upstream::HostDescriptionConstSharedPtr host) override;

  // DecoderCallback
  void onProtocolError() override;
  void onNewMessage(MySQLSession::State) override;
  void onServerGreeting(ServerGreeting&) override;
  void onClientLogin(ClientLogin&) override;
  void onClientLoginResponse(ClientLoginResponse&) override;
  void onClientSwitchResponse(ClientSwitchResponse&) override;
  void onMoreClientLoginResponse(ClientLoginResponse&) override;
  void onCommand(Command&) override;
  void onCommandResponse(CommandResponse&) override;
  // ConnectionCallback
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void sendLocal(MySQLCodec& message);
  void sendRemote(MySQLCodec& message);
  void gotoCommandPhase();
  void stepSession(uint8_t expected_seq, MySQLSession::State expected_state);

  struct UpstreamEventHandler : public Tcp::ConnectionPool::UpstreamCallbacks {
    UpstreamEventHandler(MySQLTerminalFilter& filter);
    // Network::UpstreamCallback
    void onUpstreamData(Buffer::Instance& buffer, bool end_stream) override;
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}
    MySQLTerminalFilter& parent_;
  };
  using UpstreamEventHandlerPtr = std::unique_ptr<UpstreamEventHandler>;

  friend class MySQLTerminalFitlerTest;
  friend class MySQLFilterTest;

private:
  UpstreamEventHandlerPtr upstream_event_handler_;
  RouterSharedPtr router_;
  Envoy::ConnectionPool::Cancellable* canceler_{nullptr};
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

using MySQLTerminalFilterPtr = std::unique_ptr<MySQLTerminalFilter>;
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
