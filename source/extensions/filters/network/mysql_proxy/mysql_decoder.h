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

  virtual void onProtocolError(){};
  virtual void onNewMessage(MySQLSession::State){};
  virtual void onServerGreeting(ServerGreeting&){};
  virtual void onClientLogin(ClientLogin&){};
  virtual void onClientLoginResponse(ClientLoginResponse&){};
  virtual void onClientSwitchResponse(ClientSwitchResponse&){};
  virtual void onMoreClientLoginResponse(ClientLoginResponse&){};
  virtual void onCommand(Command&){};
  virtual void onCommandResponse(CommandResponse&){};
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
  bool decode(Buffer::Instance& data, uint64_t& offset);
  void parseMessage(Buffer::Instance& message, uint64_t& offset, int seq, int len);

  DecoderCallbacks& callbacks_;
  MySQLSession session_;
  Buffer::OwnedImpl buffer_cache_;
  int cache_len_{0};
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
