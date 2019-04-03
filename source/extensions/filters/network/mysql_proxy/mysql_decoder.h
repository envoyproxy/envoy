#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/**
 * General callbacks for dispatching decoded MySQL messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}

  virtual void onProtocolError() PURE;
  virtual void onNewMessage(MySQLSession::State) PURE;
  virtual void onServerGreeting(ServerGreeting&) PURE;
  virtual void onClientLogin(ClientLogin&) PURE;
  virtual void onClientLoginResponse(ClientLoginResponse&) PURE;
  virtual void onClientSwitchResponse(ClientSwitchResponse&) PURE;
  virtual void onMoreClientLoginResponse(ClientLoginResponse&) PURE;
  virtual void onCommand(Command&) PURE;
  virtual void onCommandResponse(CommandResponse&) PURE;
};

/**
 * MySQL message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() {}

  virtual void onData(Buffer::Instance& data) PURE;
  virtual MySQLSession& getSession() PURE;
};

typedef std::unique_ptr<Decoder> DecoderPtr;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // MySQLProxy::Decoder
  void onData(Buffer::Instance& data) override;
  MySQLSession& getSession() override { return session_; }

private:
  void decode(Buffer::Instance& data, uint64_t& offset);
  void parseMessage(Buffer::Instance& message, uint64_t& offset, int seq, int len);

  DecoderCallbacks& callbacks_;
  MySQLSession session_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
