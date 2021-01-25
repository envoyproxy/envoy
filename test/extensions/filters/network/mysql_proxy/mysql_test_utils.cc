#include "mysql_test_utils.h"

#include <bits/stdint-uintn.h>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

std::string MySQLTestUtils::encodeServerGreeting(int protocol) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(protocol);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  mysql_greet_encode.setAuthPluginData(getAuthPluginData8());
  mysql_greet_encode.setServerCap(MYSQL_SERVER_CAPAB);
  mysql_greet_encode.setServerCharset(MYSQL_SERVER_LANGUAGE);
  mysql_greet_encode.setServerStatus(MYSQL_SERVER_STATUS);
  mysql_greet_encode.setExtServerCap(MYSQL_SERVER_EXT_CAPAB);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);
  BufferHelper::encodeHdr(buffer, GREETING_SEQ_NUM);
  return buffer.toString();
}

std::string MySQLTestUtils::encodeClientLogin(uint16_t client_cap, std::string user, uint8_t seq) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(client_cap);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CLIENT_CAPAB);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  mysql_clogin_encode.setUsername(user);
  mysql_clogin_encode.setAuthResp(getAuthPluginData8());
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);
  BufferHelper::encodeHdr(buffer, seq);
  return buffer.toString();
}

std::string MySQLTestUtils::encodeClientLoginResp(uint8_t srv_resp, uint8_t it, uint8_t seq_force) {
  ClientLoginResponse mysql_login_resp_encode{};
  switch (srv_resp) {
  case MYSQL_RESP_OK:
    mysql_login_resp_encode.type(Ok);
    mysql_login_resp_encode.asOkMessage().setAffectedRows(MYSQL_SM_AFFECTED_ROWS);
    mysql_login_resp_encode.asOkMessage().setLastInsertId(MYSQL_SM_LAST_ID);
    mysql_login_resp_encode.asOkMessage().setServerStatus(MYSQL_SM_SERVER_OK);
    mysql_login_resp_encode.asOkMessage().setWarnings(MYSQL_SM_SERVER_WARNINGS);
    break;
  case MYSQL_RESP_ERR:
    mysql_login_resp_encode.type(Err);
    mysql_login_resp_encode.asErrMessage().setErrorCode(MYSQL_ERROR_CODE);
    mysql_login_resp_encode.asErrMessage().setSqlStateMarker('#');
    mysql_login_resp_encode.asErrMessage().setSqlState(MySQLTestUtils::getSqlState());
    mysql_login_resp_encode.asErrMessage().setErrorMessage(MySQLTestUtils::getErrorMessage());
    break;
  case MYSQL_RESP_AUTH_SWITCH:
    mysql_login_resp_encode.type(AuthSwitch);
    mysql_login_resp_encode.asAuthSwitchMessage().setIsOldAuthSwitch(false);
    mysql_login_resp_encode.asAuthSwitchMessage().setAuthPluginData(
        MySQLTestUtils::getAuthPluginData20());
    mysql_login_resp_encode.asAuthSwitchMessage().setAuthPluginName(
        MySQLTestUtils::getAuthPluginName());
    break;
  case MYSQL_RESP_MORE:
    mysql_login_resp_encode.type(AuthMoreData);
    mysql_login_resp_encode.asAuthMoreMessage().setAuthMoreData(
        MySQLTestUtils::getAuthPluginData20());
  }

  uint8_t seq = CHALLENGE_RESP_SEQ_NUM + 2 * it;
  if (seq_force > 0) {
    seq = seq_force;
  }
  Buffer::OwnedImpl buffer;
  mysql_login_resp_encode.encode(buffer);
  BufferHelper::encodeHdr(buffer, seq);
  return buffer.toString();
}

std::string MySQLTestUtils::encodeAuthSwitchResp() {
  ClientSwitchResponse mysql_switch_resp_encode{};
  std::string resp_opaque_data("mysql_opaque");
  mysql_switch_resp_encode.setAuthPluginResp(resp_opaque_data);
  Buffer::OwnedImpl buffer;
  mysql_switch_resp_encode.encode(buffer);
  BufferHelper::encodeHdr(buffer, AUTH_SWITH_RESP_SEQ);
  return buffer.toString();
}

int MySQLTestUtils::sizeOfLengthEncodeInteger(uint64_t val) {
  if (val < 251) {
    return sizeof(uint8_t);
  } else if (val < (1 << 16)) {
    return sizeof(uint8_t) + sizeof(uint16_t);
  } else if (val < (1 << 24)) {
    return sizeof(uint8_t) + sizeof(uint8_t) * 3;
  } else {
    return sizeof(uint8_t) + sizeof(uint64_t);
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
