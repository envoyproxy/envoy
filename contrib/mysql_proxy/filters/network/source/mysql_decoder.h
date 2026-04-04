#pragma once

#include "contrib/common/sqlutils/source/sqlutils.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_command.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_switch_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_session.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

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
  virtual void onServerGreeting(ServerGreeting&) PURE;
  virtual void onClientLogin(ClientLogin&, MySQLSession::State) PURE;
  virtual void onClientLoginResponse(ClientLoginResponse&) PURE;
  virtual void onClientSwitchResponse(ClientSwitchResponse&) PURE;
  virtual void onMoreClientLoginResponse(ClientLoginResponse&) PURE;
  virtual void onCommand(Command&) PURE;
  virtual void onCommandResponse(CommandResponse&) PURE;
  virtual void onAuthSwitchMoreClientData(std::unique_ptr<SecureBytes> data) PURE;
  virtual bool onSSLRequest() PURE;
};

/**
 * MySQL message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  enum class Result {
    ReadyForNext, // Decoder processed previous message and is ready for the next message.
    Stopped // Received and processed message disrupts the current flow. Decoder stopped accepting
            // data. This happens when decoder wants filter to perform some action, for example to
            // call starttls transport socket to enable TLS.
  };

  struct PayloadMetadata {
    uint8_t seq;
    uint32_t len;
  };

  virtual Result onData(Buffer::Instance& data, bool is_upstream) PURE;
  virtual MySQLSession& getSession() PURE;

  const Extensions::Common::SQLUtils::SQLUtils::DecoderAttributes& getAttributes() const {
    return attributes_;
  }

  const std::vector<PayloadMetadata>& getPayloadMetadataList() const {
    return payload_metadata_list_;
  }

protected:
  // Decoder attributes.
  Extensions::Common::SQLUtils::SQLUtils::DecoderAttributes attributes_;
  std::vector<PayloadMetadata> payload_metadata_list_{};
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
