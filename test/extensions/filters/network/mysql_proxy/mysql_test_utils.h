#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_loginok.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"

namespace Envoy {
    namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

#define MYSQL_VER_MAJOR 5
#define MYSQL_VER_MINOR 0
#define MYSQL_VER_VAR 54
#define MYSQL_SM_LAST_ID 0
#define MYSQL_SM_SERVER_OK 0
#define MYSQL_SM_SERVER_WARNINGS 0x0001
#define MYSQL_SM_AFFECTED_ROWS 1
#define CLIENT_NUM 10
#define PARALLEL_SESSIONS 4

class MysqlTestUtils {

public:
  static std::string GetSalt() { return "!@salt#$"; }
  static std::string GetAuthResp() { return "p4$$w0r6"; }
  static std::string GetVersion();

  std::string EncodeServerGreeting(int protocol);
  std::string EncodeClientLogin(uint16_t client_cap, std::string user);
  std::string EncodeClientLoginResp(uint8_t srv_resp, int it = 0);
  std::string EncodeAuthSwitchResp();
};

std::string MysqlTestUtils::GetVersion() {
  std::string ver(std::to_string(MYSQL_VER_MAJOR));
  ver.append(".");
  ver.append(std::to_string(MYSQL_VER_MINOR));
  ver.append(".");
  ver.append(std::to_string(MYSQL_VER_VAR));
  return ver;
}

std::string MysqlTestUtils::EncodeServerGreeting(int protocol) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.SetProtocol(protocol);
  std::string ver(MysqlTestUtils::GetVersion());
  mysql_greet_encode.SetVersion(ver);
  mysql_greet_encode.SetThreadId(MYSQL_THREAD_ID);
  std::string salt(GetSalt());
  mysql_greet_encode.SetSalt(salt);
  mysql_greet_encode.SetServerCap(MYSQL_SERVER_CAPAB);
  mysql_greet_encode.SetServerLanguage(MYSQL_SERVER_LANGUAGE);
  mysql_greet_encode.SetServerStatus(MYSQL_SERVER_STATUS);
  mysql_greet_encode.SetExtServerCap(MYSQL_SERVER_EXT_CAPAB);
  std::string data = mysql_greet_encode.Encode();
  std::string mysql_msg = mysql_greet_encode.EncodeHdr(data, GREETING_SEQ_NUM);
  return mysql_msg;
}

std::string MysqlTestUtils::EncodeClientLogin(uint16_t client_cap, std::string user) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.SetClientCap(client_cap);
  mysql_clogin_encode.SetExtendedClientCap(MYSQL_EXT_CLIENT_CAPAB);
  mysql_clogin_encode.SetMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.SetCharset(MYSQL_CHARSET);
  mysql_clogin_encode.SetUsername(user);
  std::string auth_resp(GetAuthResp());
  mysql_clogin_encode.SetAuthResp(auth_resp);
  std::string data = mysql_clogin_encode.Encode();
  std::string mysql_msg = mysql_clogin_encode.EncodeHdr(data, CHALLENGE_SEQ_NUM);
  return mysql_msg;
}

std::string MysqlTestUtils::EncodeClientLoginResp(uint8_t srv_resp, int it) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.SetRespCode(srv_resp);
  mysql_loginok_encode.SetAffectedRows(MYSQL_SM_AFFECTED_ROWS);
  mysql_loginok_encode.SetLastInsertId(MYSQL_SM_LAST_ID);
  mysql_loginok_encode.SetServerStatus(MYSQL_SM_SERVER_OK);
  mysql_loginok_encode.SetWarnings(MYSQL_SM_SERVER_WARNINGS);
  std::string data = mysql_loginok_encode.Encode();
  int seq = CHALLENGE_RESP_SEQ_NUM + 2 * it;
  std::string mysql_msg = mysql_loginok_encode.EncodeHdr(data, seq);
  return mysql_msg;
}

std::string MysqlTestUtils::EncodeAuthSwitchResp() {
  ClientSwitchResponse mysql_switch_resp_encode{};
  std::string resp_opaque_data("mysql_opaque");
  mysql_switch_resp_encode.SetAuthPluginResp(resp_opaque_data);
  std::string data = mysql_switch_resp_encode.Encode();
  std::string mysql_msg = mysql_switch_resp_encode.EncodeHdr(data, AUTH_SWITH_RESP_SEQ);
  return mysql_msg;
}

} // namespace MysqlProxy
} // namespace NetworkFilters
 } // namespace Extensions
} // namespace Envoy
