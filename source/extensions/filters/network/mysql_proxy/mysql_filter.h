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
 * Implementation of MySQL monitor filter, just decode message and collect stats.
 */
class MySQLMoniterFilter : public Network::Filter,
                           public DecoderCallbacks,
                           public Logger::Loggable<Logger::Id::filter> {
public:
  MySQLMoniterFilter(MySQLFilterConfigSharedPtr config, DecoderFactory& factory);
  ~MySQLMoniterFilter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // MySQLProxy::DecoderCallback
  void onProtocolError() override;
  void onNewMessage(MySQLSession::State state) override;
  void onServerGreeting(ServerGreeting&) override{};
  void onClientLogin(ClientLogin& message) override;
  void onClientLoginResponse(ClientLoginResponse& message) override;
  void onClientSwitchResponse(ClientSwitchResponse&) override{};
  void onMoreClientLoginResponse(ClientLoginResponse& message) override;
  void onCommand(Command& message) override;
  void onCommandResponse(CommandResponse&) override{};

  void doDecode(Buffer::Instance& buffer);
  void clearDynamicData();
  DecoderPtr createDecoder(DecoderCallbacks& callbacks);
  MySQLSession& getSession() { return decoder_->getSession(); }

protected:
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  DecoderPtr decoder_;
  MySQLFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};

private:
  bool sniffing_{true};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
