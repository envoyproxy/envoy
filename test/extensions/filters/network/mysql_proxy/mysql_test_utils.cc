#include "mysql_test_utils.h"

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
  std::string salt(getSalt());
  mysql_greet_encode.setSalt(salt);
  mysql_greet_encode.setServerCap(MYSQL_SERVER_CAPAB);
  mysql_greet_encode.setServerLanguage(MYSQL_SERVER_LANGUAGE);
  mysql_greet_encode.setServerStatus(MYSQL_SERVER_STATUS);
  mysql_greet_encode.setExtServerCap(MYSQL_SERVER_EXT_CAPAB);
  std::string data = mysql_greet_encode.encode();
  std::string mysql_msg = BufferHelper::encodeHdr(data, GREETING_SEQ_NUM);
  return mysql_msg;
}

std::string MySQLTestUtils::encodeClientLogin(uint16_t client_cap, std::string user, uint8_t seq) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(client_cap);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CLIENT_CAPAB);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  mysql_clogin_encode.setUsername(user);
  std::string auth_resp(getAuthResp());
  mysql_clogin_encode.setAuthResp(auth_resp);
  std::string data = mysql_clogin_encode.encode();
  std::string mysql_msg = BufferHelper::encodeHdr(data, seq);
  return mysql_msg;
}

std::string MySQLTestUtils::encodeClientLoginResp(uint8_t srv_resp, uint8_t it, uint8_t seq_force) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(srv_resp);
  mysql_loginok_encode.setAffectedRows(MYSQL_SM_AFFECTED_ROWS);
  mysql_loginok_encode.setLastInsertId(MYSQL_SM_LAST_ID);
  mysql_loginok_encode.setServerStatus(MYSQL_SM_SERVER_OK);
  mysql_loginok_encode.setWarnings(MYSQL_SM_SERVER_WARNINGS);
  std::string data = mysql_loginok_encode.encode();
  uint8_t seq = CHALLENGE_RESP_SEQ_NUM + 2 * it;
  if (seq_force > 0) {
    seq = seq_force;
  }
  std::string mysql_msg = BufferHelper::encodeHdr(data, seq);
  return mysql_msg;
}

std::string MySQLTestUtils::encodeAuthSwitchResp() {
  ClientSwitchResponse mysql_switch_resp_encode{};
  std::string resp_opaque_data("mysql_opaque");
  mysql_switch_resp_encode.setAuthPluginResp(resp_opaque_data);
  std::string data = mysql_switch_resp_encode.encode();
  std::string mysql_msg = BufferHelper::encodeHdr(data, AUTH_SWITH_RESP_SEQ);
  return mysql_msg;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
