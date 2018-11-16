#pragma once
#include <iostream>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int MYSQL_VER_MAJOR = 5;
constexpr int MYSQL_VER_MINOR = 0;
constexpr int MYSQL_VER_VAR = 54;
constexpr int MYSQL_SM_LAST_ID = 0;
constexpr int MYSQL_SM_SERVER_OK = 0;
constexpr int MYSQL_SM_SERVER_WARNINGS = 0x0001;
constexpr int MYSQL_SM_AFFECTED_ROWS = 1;
constexpr int CLIENT_NUM = 10;
constexpr int PARALLEL_SESSIONS = 4;

class MySQLTestUtils {

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

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
