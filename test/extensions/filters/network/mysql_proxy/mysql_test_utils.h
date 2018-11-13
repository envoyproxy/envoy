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
  static std::string GetVersion() {
      std::string ver(std::to_string(MYSQL_VER_MAJOR));
      ver.append(".");
      ver.append(std::to_string(MYSQL_VER_MINOR));
      ver.append(".");
      ver.append(std::to_string(MYSQL_VER_VAR));
      return ver;
  }

  std::string EncodeServerGreeting(int protocol);
  std::string EncodeClientLogin(uint16_t client_cap, std::string user);
  std::string EncodeClientLoginResp(uint8_t srv_resp, int it = 0);
  std::string EncodeAuthSwitchResp();
};

} // namespace MysqlProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
