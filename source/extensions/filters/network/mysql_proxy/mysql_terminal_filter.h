#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "envoy/tcp/conn_pool.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/route.h"
#include "extensions/filters/network/mysql_proxy/stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * Implementation of MySQL proxy filter.
 */
class MySQLTerminalFilter : public Tcp::ConnectionPool::Callbacks,
                            public Network::ReadFilter,
                            public Logger::Loggable<Logger::Id::filter> {

  friend class MySQLFilterTest;
  friend class MySQLTerminalFitlerTest;

public:
  MySQLTerminalFilter(MySQLFilterConfigSharedPtr config, RouterSharedPtr router,
                      DecoderFactory& decoder_factory);
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

  struct DownstreamDecoder : public DecoderCallbacks, public Network::ConnectionCallbacks {
    DownstreamDecoder(MySQLTerminalFilter& filter);
    Network::FilterStatus onData(Buffer::Instance& buffer, bool);
    // DecoderCallbacks
    void onProtocolError() override;
    void onNewMessage(MySQLSession::State state) override;
    void onServerGreeting(ServerGreeting&) override {}
    void onClientLogin(ClientLogin& message) override;
    void onClientLoginResponse(ClientLoginResponse&) override {}
    void onClientSwitchResponse(ClientSwitchResponse&) override;
    void onMoreClientLoginResponse(ClientLoginResponse&) override{};
    void onCommand(Command& message) override;
    void onCommandResponse(CommandResponse&) override {}
    // ConnectionCallback
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}
    void send(MySQLCodec& message);

    void passAuth();
    DecoderPtr decoder_;
    Buffer::OwnedImpl buffer_;
    MySQLTerminalFilter& parent_;
  };

  struct UpstreamDecoder : public DecoderCallbacks, public Tcp::ConnectionPool::UpstreamCallbacks {
    UpstreamDecoder(MySQLTerminalFilter& filter);

    // DecoderCallbacks
    void onProtocolError() override;
    void onNewMessage(MySQLSession::State) override;
    void onServerGreeting(ServerGreeting&) override;
    void onClientLogin(ClientLogin&) override {}
    void onClientLoginResponse(ClientLoginResponse& message) override;
    void onClientSwitchResponse(ClientSwitchResponse&) override {}
    void onMoreClientLoginResponse(ClientLoginResponse& message) override;
    void onCommand(Command&) override {}
    void onCommandResponse(CommandResponse&) override;

    // Network::UpstreamCallback
    void onUpstreamData(Buffer::Instance& buffer, bool end_stream) override;
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}
    void send(MySQLCodec& message);
    DecoderPtr decoder_;
    Buffer::OwnedImpl buffer_;
    MySQLTerminalFilter& parent_;
  };

  using UpstreamDecoderPtr = std::unique_ptr<UpstreamDecoder>;
  using DownstreamDecoderPtr = std::unique_ptr<DownstreamDecoder>;

  void gotoCommandPhase();
  void stepSession(MySQLSession& session, uint8_t expected_seq, MySQLSession::State expected_state);
  void stepClientSession(uint8_t expected_seq, MySQLSession::State expected_state);
  void stepServerSession(uint8_t expected_seq, MySQLSession::State expected_state);
  DecoderPtr createDecoder(DecoderCallbacks& callbacks);
  friend class MySQLTerminalFitlerTest;
  friend class MySQLFilterTest;

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  MySQLFilterConfigSharedPtr config_;
  DownstreamDecoderPtr downstream_decoder_;
  UpstreamDecoderPtr upstream_decoder_;
  RouterSharedPtr router_;
  DecoderFactory& decoder_factory_;
  Envoy::ConnectionPool::Cancellable* canceler_{nullptr};
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

using MySQLTerminalFilterPtr = std::unique_ptr<MySQLTerminalFilter>;
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
