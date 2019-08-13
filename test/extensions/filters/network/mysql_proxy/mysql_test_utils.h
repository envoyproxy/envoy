#pragma once
#include "fmt/format.h"

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
  static std::string getSalt() { return "!@salt#$"; }
  static std::string getAuthResp() { return "p4$$w0r6"; }
  static std::string getVersion() {
    return fmt::format("{0}.{1}.{2}", MYSQL_VER_MAJOR, MYSQL_VER_MINOR, MYSQL_VER_VAR);
  }

  std::string encodeServerGreeting(int protocol);
  std::string encodeClientLogin(uint16_t client_cap, std::string user, uint8_t seq);
  std::string encodeClientLoginResp(uint8_t srv_resp, uint8_t it = 0, uint8_t seq_force = 0);
  std::string encodeAuthSwitchResp();
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
