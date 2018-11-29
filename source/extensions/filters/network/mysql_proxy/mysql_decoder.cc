#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

#include <arpa/inet.h>

#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void DecoderImpl::parseMessage(Buffer::Instance& message, uint64_t& offset, int seq, int len) {
  ENVOY_LOG(trace, "mysql_proxy: parsing message, offset {}, seq {}, len {}", offset, seq, len);

  // Run the MySQL state machine
  switch (session_.getState()) {

  // Expect Server Challenge packet
  case MySQLSession::State::MYSQL_INIT: {
    ServerGreeting greeting;
    greeting.decode(message, offset, seq, len);
    callbacks_.onServerGreeting(greeting);

    session_.setState(MySQLSession::State::MYSQL_CHALLENGE_REQ);
    break;
  }

  // Process Client Handshake Response
  case MySQLSession::State::MYSQL_CHALLENGE_REQ: {
    ClientLogin client_login;
    client_login.decode(message, offset, seq, len);
    callbacks_.onClientLogin(client_login);

    if (client_login.isSSLRequest()) {
      session_.setState(MySQLSession::State::MYSQL_SSL_PT);
    } else if (client_login.isResponse41()) {
      session_.setState(MySQLSession::State::MYSQL_CHALLENGE_RESP_41);
    } else {
      session_.setState(MySQLSession::State::MYSQL_CHALLENGE_RESP_320);
    }
    break;
  }

  case MySQLSession::State::MYSQL_SSL_PT:
    break;

  case MySQLSession::State::MYSQL_CHALLENGE_RESP_41:
  case MySQLSession::State::MYSQL_CHALLENGE_RESP_320: {
    ClientLoginResponse client_login_resp;
    client_login_resp.decode(message, offset, seq, len);
    callbacks_.onClientLoginResponse(client_login_resp);

    if (client_login_resp.getRespCode() == MYSQL_RESP_OK) {
      session_.setState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_AUTH_SWITCH) {
      session_.setState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
      session_.setState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.setState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_RESP: {
    ClientSwitchResponse client_switch_resp;
    client_switch_resp.decode(message, offset, seq, len);
    callbacks_.onClientSwitchResponse(client_switch_resp);

    session_.setState(MySQLSession::State::MYSQL_AUTH_SWITCH_MORE);
    break;
  }

  case MySQLSession::State::MYSQL_AUTH_SWITCH_MORE: {
    ClientLoginResponse client_login_resp;
    client_login_resp.decode(message, offset, seq, len);
    callbacks_.onMoreClientLoginResponse(client_login_resp);

    if (client_login_resp.getRespCode() == MYSQL_RESP_OK) {
      session_.setState(MySQLSession::State::MYSQL_REQ);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_MORE) {
      session_.setState(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP);
    } else if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
      session_.setState(MySQLSession::State::MYSQL_ERROR);
    } else {
      session_.setState(MySQLSession::State::MYSQL_NOT_HANDLED);
    }
    break;
  }

  // Process Command
  case MySQLSession::State::MYSQL_REQ: {
    Command command;
    command.decode(message, offset, seq, len);
    callbacks_.onCommand(command);

    session_.setState(MySQLSession::State::MYSQL_REQ_RESP);
    break;
  }

  // Process Command Response
  case MySQLSession::State::MYSQL_REQ_RESP: {
    CommandResponse command_resp;
    command_resp.decode(message, offset, seq, len);
    callbacks_.onCommandResponse(command_resp);

    session_.setState(MySQLSession::State::MYSQL_REQ);
    break;
  }

  case MySQLSession::State::MYSQL_ERROR:
  case MySQLSession::State::MYSQL_NOT_HANDLED:
  default:
    break;
  }

  ENVOY_LOG(trace, "mysql_proxy: msg parsed, session in state {}",
            static_cast<int>(session_.getState()));
}

bool DecoderImpl::decode(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "mysql_proxy: decoding {} bytes at offset {}", data.length(), offset);

  int len = 0;
  int seq = 0;
  if (BufferHelper::peekHdr(data, offset, len, seq) != MYSQL_SUCCESS) {
    throw EnvoyException("error parsing mysql packet header");
  }

  callbacks_.onNewMessage(session_.getState());

  /*
  // The sequence ID is reset on a new command.
  if (seq == MYSQL_PKT_0) {
    session_.setExpectedSeq(MYSQL_PKT_0);
    ENVOY_LOG(trace, "mysql_proxy: received packet with sequence ID = 0");
  }
  */

  // Ignore duplicate and out-of-sync packets.
  if (seq != session_.getExpectedSeq()) {
    callbacks_.onProtocolError();
    offset += len;
    ENVOY_LOG(info, "mysql_proxy: ignoring out-of-sync packet");
    return true;
  }
  session_.setExpectedSeq(seq + 1);

  // Ensure that the whole packet was consumed.
  const uint64_t prev_offset = offset;
  parseMessage(data, offset, seq, len);
  offset = prev_offset + len;

  ENVOY_LOG(trace, "mysql_proxy: offset after decoding is {} out of {}", offset, data.length());
  return true;
}

void DecoderImpl::onData(Buffer::Instance& data) {
  // TODO(venilnoronha): handle messages over 16 mb. See
  // https://dev.mysql.com/doc/dev/mysql-server/8.0.2/page_protocol_basic_packets.html#sect_protocol_basic_packets_sending_mt_16mb.
  uint64_t offset = 0;
  while (!BufferHelper::endOfBuffer(data, offset) && decode(data, offset)) {
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
