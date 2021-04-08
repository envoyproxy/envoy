#pragma once

#include "extensions/common/sqlutils/sqlutils.h"
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
  virtual ~DecoderCallbacks() = default;

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
  virtual ~Decoder() = default;

  virtual void onData(Buffer::Instance& data) PURE;
  virtual MySQLSession& getSession() PURE;

  const Extensions::Common::SQLUtils::SQLUtils::DecoderAttributes& getAttributes() const {
    return attributes_;
  }

protected:
  // Decoder attributes.
  Extensions::Common::SQLUtils::SQLUtils::DecoderAttributes attributes_;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderFactory {
public:
  virtual ~DecoderFactory() = default;
  virtual DecoderPtr create(DecoderCallbacks& callbacks) PURE;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
