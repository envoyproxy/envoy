#include "contrib/mysql_proxy/filters/network/source/mysql_decoder_impl.h"

#include "source/common/common/logger.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void DecoderImpl::parseMessage(Buffer::Instance& message, uint8_t seq, uint32_t len) {
  ENVOY_LOG(trace, "mysql_proxy: parsing message, seq {}, len {}", seq, len);
  // Run the MySQL state machine
  switch (session_.getState()) {
  case MySQLSession::State::Init: {
    // Expect Server Challenge packet
    ServerGreeting greeting;
    greeting.decode(message, seq, len);
    session_.setState(MySQLSession::State::ChallengeReq);
    callbacks_.onServerGreeting(greeting);
    break;
  }
  case MySQLSession::State::ChallengeReq: {
    // Process Client Handshake Response
    ClientLogin client_login{};
    client_login.decode(message, seq, len);
    if (client_login.isSSLRequest()) {
      session_.setState(MySQLSession::State::SslPt);
    } else if (client_login.isResponse41()) {
      session_.setState(MySQLSession::State::ChallengeResp41);
    } else {
      session_.setState(MySQLSession::State::ChallengeResp320);
    }
    callbacks_.onClientLogin(client_login);
    break;
  }
  case MySQLSession::State::SslPt:
    // just consume
    message.drain(len);
    break;
  case MySQLSession::State::ChallengeResp41:
  case MySQLSession::State::ChallengeResp320: {
    uint8_t resp_code;
    if (BufferHelper::peekUint8(message, resp_code) != DecodeStatus::Success) {
      session_.setState(MySQLSession::State::NotHandled);
      break;
    }
    std::unique_ptr<ClientLoginResponse> msg;
    MySQLSession::State state = MySQLSession::State::NotHandled;
    switch (resp_code) {
    case MYSQL_RESP_OK: {
      msg = std::make_unique<OkMessage>();
      state = MySQLSession::State::Req;
      // reset seq# when entering the REQ state
      session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
      break;
    }
    case MYSQL_RESP_AUTH_SWITCH: {
      msg = std::make_unique<AuthSwitchMessage>();
      state = MySQLSession::State::AuthSwitchResp;
      break;
    }
    case MYSQL_RESP_ERR: {
      msg = std::make_unique<ErrMessage>();
      state = MySQLSession::State::Error;
      break;
    }
    case MYSQL_RESP_MORE: {
      msg = std::make_unique<AuthMoreMessage>();
      break;
    }
    default:
      session_.setState(state);
      return;
    }
    msg->decode(message, seq, len);
    session_.setState(state);
    callbacks_.onClientLoginResponse(*msg);
    break;
  }

  case MySQLSession::State::AuthSwitchResp: {
    ClientSwitchResponse client_switch_resp{};
    client_switch_resp.decode(message, seq, len);
    session_.setState(MySQLSession::State::AuthSwitchMore);
    callbacks_.onClientSwitchResponse(client_switch_resp);
    break;
  }

  case MySQLSession::State::AuthSwitchMore: {
    uint8_t resp_code;
    if (BufferHelper::peekUint8(message, resp_code) != DecodeStatus::Success) {
      session_.setState(MySQLSession::State::NotHandled);
      break;
    }
    std::unique_ptr<ClientLoginResponse> msg;
    MySQLSession::State state = MySQLSession::State::NotHandled;
    switch (resp_code) {
    case MYSQL_RESP_OK: {
      msg = std::make_unique<OkMessage>();
      state = MySQLSession::State::Req;
      session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
      break;
    }
    case MYSQL_RESP_MORE: {
      msg = std::make_unique<AuthMoreMessage>();
      state = MySQLSession::State::AuthSwitchResp;
      break;
    }
    case MYSQL_RESP_ERR: {
      msg = std::make_unique<ErrMessage>();
      // stop parsing auth req/response, attempt to resync in command state
      state = MySQLSession::State::Resync;
      session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
      break;
    }
    case MYSQL_RESP_AUTH_SWITCH: {
      msg = std::make_unique<AuthSwitchMessage>();
      break;
    }
    default:
      session_.setState(state);
      return;
    }
    msg->decode(message, seq, len);
    session_.setState(state);
    callbacks_.onMoreClientLoginResponse(*msg);
    break;
  }

  case MySQLSession::State::Resync: {
    // re-sync to MYSQL_REQ state
    // expected seq check succeeded, no need to verify
    session_.setState(MySQLSession::State::Req);
    FALLTHRU;
  }

  // Process Command
  case MySQLSession::State::Req: {
    Command command{};
    command.decode(message, seq, len);
    session_.setState(MySQLSession::State::ReqResp);
    callbacks_.onCommand(command);
    break;
  }

  // Process Command Response
  case MySQLSession::State::ReqResp: {
    CommandResponse command_resp{};
    command_resp.decode(message, seq, len);
    callbacks_.onCommandResponse(command_resp);
    break;
  }

  case MySQLSession::State::Error:
  case MySQLSession::State::NotHandled:
  default:
    break;
  }

  ENVOY_LOG(trace, "mysql_proxy: msg parsed, session in state {}",
            static_cast<int>(session_.getState()));
}

bool DecoderImpl::decode(Buffer::Instance& data) {
  ENVOY_LOG(trace, "mysql_proxy: decoding {} bytes", data.length());
  uint32_t len = 0;
  uint8_t seq = 0;

  // ignore ssl message
  if (session_.getState() == MySQLSession::State::SslPt) {
    data.drain(data.length());
    return true;
  }

  if (BufferHelper::peekHdr(data, len, seq) != DecodeStatus::Success) {
    throw EnvoyException("error parsing mysql packet header");
  }
  ENVOY_LOG(trace, "mysql_proxy: seq {}, len {}", seq, len);
  // If message is split over multiple packets, hold off until the entire message is available.
  // Consider the size of the header here as it's not consumed yet.
  if (sizeof(uint32_t) + len > data.length()) {
    return false;
  }

  BufferHelper::consumeHdr(data); // Consume the header once the message is fully available.
  callbacks_.onNewMessage(session_.getState());

  // Ignore duplicate and out-of-sync packets.
  if (seq != session_.getExpectedSeq()) {
    // case when server response is over, and client send req
    if (session_.getState() == MySQLSession::State::ReqResp && seq == MYSQL_REQUEST_PKT_NUM) {
      session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
      session_.setState(MySQLSession::State::Req);
    } else {
      ENVOY_LOG(info, "mysql_proxy: ignoring out-of-sync packet");
      callbacks_.onProtocolError();
      data.drain(len); // Ensure that the whole message was consumed
      return true;
    }
  }
  session_.setExpectedSeq(seq + 1);

  const ssize_t data_len = data.length();
  parseMessage(data, seq, len);
  const ssize_t consumed_len = data_len - data.length();
  data.drain(len - consumed_len); // Ensure that the whole message was consumed

  ENVOY_LOG(trace, "mysql_proxy: {} bytes remaining in buffer", data.length());
  return true;
}

void DecoderImpl::onData(Buffer::Instance& data) {
  // TODO(venilnoronha): handle messages over 16 mb. See
  // https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_packets.html#sect_protocol_basic_packets_sending_mt_16mb.
  while (!BufferHelper::endOfBuffer(data) && decode(data)) {
  }
}

DecoderFactoryImpl DecoderFactoryImpl::instance_;

DecoderPtr DecoderFactoryImpl::create(DecoderCallbacks& callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
