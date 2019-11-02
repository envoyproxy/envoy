#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

#include <arpa/inet.h>

#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void DecoderImpl::parseMessage(Buffer::Instance& message, uint8_t seq, uint32_t len) {
  ENVOY_LOG(trace, "mysql_proxy: parsing message, seq {}, len {}", seq, len);

  // Run the MySQL state machine
  switch (session_.getState()) {

  // Expect Server Challenge packet
  case MySQLSession::State::Init: {
    ServerGreeting greeting;
    greeting.decode(message, seq, len);
    callbacks_.onServerGreeting(greeting);

    session_.setState(MySQLSession::State::ChallengeReq);
    break;
  }

  // Process Client Handshake Response
  case MySQLSession::State::ChallengeReq: {
    ClientLogin client_login{};
    client_login.decode(message, seq, len);
    callbacks_.onClientLogin(client_login);

    if (client_login.isSSLRequest()) {
      session_.setState(MySQLSession::State::SslPt);
    } else if (client_login.isResponse41()) {
      session_.setState(MySQLSession::State::ChallengeResp41);
    } else {
      session_.setState(MySQLSession::State::ChallengeResp320);
    }
    break;
  }

  case MySQLSession::State::SslPt:
    break;

  case MySQLSession::State::ChallengeResp41:
  case MySQLSession::State::ChallengeResp320: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.decode(message, seq, len);
    callbacks_.onClientLoginResponse(client_login_resp);

    if (client_login_resp.getRespCode() == MYSQL_RESP_OK) {
      session_.setState(MySQLSession::State::Req);
      // reset seq# when entering the REQ state
      session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_AUTH_SWITCH) {
      session_.setState(MySQLSession::State::AuthSwitchResp);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
      // client/server should close the connection:
      // https://dev.mysql.com/doc/internals/en/connection-phase.html
      session_.setState(MySQLSession::State::Error);
    } else {
      session_.setState(MySQLSession::State::NotHandled);
    }
    break;
  }

  case MySQLSession::State::AuthSwitchResp: {
    ClientSwitchResponse client_switch_resp{};
    client_switch_resp.decode(message, seq, len);
    callbacks_.onClientSwitchResponse(client_switch_resp);

    session_.setState(MySQLSession::State::AuthSwitchMore);
    break;
  }

  case MySQLSession::State::AuthSwitchMore: {
    ClientLoginResponse client_login_resp{};
    client_login_resp.decode(message, seq, len);
    callbacks_.onMoreClientLoginResponse(client_login_resp);

    if (client_login_resp.getRespCode() == MYSQL_RESP_OK) {
      session_.setState(MySQLSession::State::Req);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_MORE) {
      session_.setState(MySQLSession::State::AuthSwitchResp);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
      // stop parsing auth req/response, attempt to resync in command state
      session_.setState(MySQLSession::State::Resync);
      session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
    } else {
      session_.setState(MySQLSession::State::NotHandled);
    }
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
    callbacks_.onCommand(command);

    session_.setState(MySQLSession::State::ReqResp);
    break;
  }

  // Process Command Response
  case MySQLSession::State::ReqResp: {
    CommandResponse command_resp{};
    command_resp.decode(message, seq, len);
    callbacks_.onCommandResponse(command_resp);

    session_.setState(MySQLSession::State::Req);
    session_.setExpectedSeq(MYSQL_REQUEST_PKT_NUM);
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
  if (BufferHelper::peekHdr(data, len, seq) != MYSQL_SUCCESS) {
    throw EnvoyException("error parsing mysql packet header");
  }

  // If message is split over multiple packets, hold off until the entire message is available.
  // Consider the size of the header here as it's not consumed yet.
  if (sizeof(uint32_t) + len > data.length()) {
    return false;
  }

  BufferHelper::consumeHdr(data); // Consume the header once the message is fully available.
  callbacks_.onNewMessage(session_.getState());

  // Ignore duplicate and out-of-sync packets.
  if (seq != session_.getExpectedSeq()) {
    callbacks_.onProtocolError();
    ENVOY_LOG(info, "mysql_proxy: ignoring out-of-sync packet");
    data.drain(len); // Ensure that the whole message was consumed
    return true;
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

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
