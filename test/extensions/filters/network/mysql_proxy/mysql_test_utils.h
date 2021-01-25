#pragma once
#include <bits/stdint-uintn.h>

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

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
constexpr uint32_t MYSQL_SERVER_CAP_AUTH_PLUGIN = 0x00080000;
constexpr uint32_t MYSQL_SERVER_SECURE_CONNECTION = 0x00008000;
constexpr uint16_t MYSQL_ERROR_CODE = MYSQL_CR_AUTH_PLUGIN_ERR;

class MySQLTestUtils {

public:
  static std::string getAuthPluginData8() { return "!@salt#$"; }
  static std::string getAuthPluginData20() { return "!@salt#$!@salt#$xxXX"; }
  static std::string getAuthResp8() { return "p4$$w0r6"; }
  static std::string getAuthResp20() { return "p4$$w0r6p4$$w0r61111"; }
  static std::string getVersion() {
    return fmt::format("{0}.{1}.{2}", MYSQL_VER_MAJOR, MYSQL_VER_MINOR, MYSQL_VER_VAR);
  }
  static std::string getSqlState() { return "HY000"; }
  static std::string getErrorMessage() { return "auth failed"; }
  static std::string getAuthPluginName() { return "mysql_native_password"; }
  static int sizeOfLengthEncodeInteger(uint64_t val);

  std::string encodeServerGreeting(int protocol);
  std::string encodeClientLogin(uint16_t client_cap, std::string user, uint8_t seq);
  std::string encodeClientLoginResp(uint8_t srv_resp, uint8_t it = 0, uint8_t seq_force = 0);
  std::string encodeAuthSwitchResp();
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
