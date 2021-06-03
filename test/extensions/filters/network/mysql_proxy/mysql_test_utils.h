#pragma once
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

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
  static std::vector<uint8_t> getAuthPluginData8() { return getAuthResp8(); }
  static std::vector<uint8_t> getAuthPluginData20() { return getAuthResp20(); }
  static std::vector<uint8_t> getAuthResp8() { return std::vector<uint8_t>(8, 0xff); }
  static std::vector<uint8_t> getAuthResp20() { return std::vector<uint8_t>(20, 0xff); }
  static std::string getVersion() {
    return fmt::format("{0}.{1}.{2}", MYSQL_VER_MAJOR, MYSQL_VER_MINOR, MYSQL_VER_VAR);
  }
  static std::string getUsername() { return "username"; }
  static std::string getSqlState() { return "HY000"; }
  static std::string getErrorMessage() { return "auth failed"; }
  static std::string getAuthPluginName() { return "mysql_native_password"; }
  static std::string getDb() { return "mysql.db"; }
  static std::string getCommandResponse() { return "command response"; }
  static std::string getInfo() { return "info"; }
  static int
  bytesOfConnAtrributeLength(const std::vector<std::pair<std::string, std::string>> conn);
  static int bytesOfEncodedInteger(int64_t len);
  static int sizeOfLengthEncodeInteger(uint64_t val);

  std::string encodeServerGreeting(int protocol);
  std::string encodeClientLogin(uint16_t client_cap, std::string user, uint8_t seq);
  std::string encodeClientLoginResp(uint8_t srv_resp, uint8_t it = 0, uint8_t seq_force = 0);
  std::string encodeAuthSwitchResp();
  std::string encodeMessage(uint32_t packet_len, uint8_t it = 0, uint8_t seq_force = 0);
};

/**
 * Message helper of MySQL, generate MySQL packs.
 */
class MessageHelper {
public:
  static ClientLogin encodeClientLogin(const std::string& username, const std::string& db,
                                       const std::vector<uint8_t>& auth_resp);
  static ClientLogin encodeSslUpgrade();
  static ServerGreeting encodeGreeting(const std::vector<uint8_t>& seed) {
    return encodeGreeting(seed, "mysql_native_password");
  }
  static ServerGreeting encodeGreeting(const std::vector<uint8_t>& seed,
                                       const std::string& auth_plugin_name);
  static OkMessage encodeOk();
  static AuthSwitchMessage encodeAuthSwitch(const std::vector<uint8_t>& seed);
  static AuthSwitchMessage encodeAuthSwitch(const std::vector<uint8_t>& seed,
                                            const std::string& auth_plugin_name);
  static AuthMoreMessage encodeAuthMore(const std::vector<uint8_t>& seed);
  static ErrMessage encodeErr(uint16_t error_code, uint8_t sql_marker, std::string&& sql_state,
                              std::string&& error_message);
  static ErrMessage passwordLengthError(int len);
  static ErrMessage authError(const std::string& username, const std::string& destination,
                              bool using_password);
  static ErrMessage dbError(const std::string& db);
  static ClientSwitchResponse encodeSwithResponse(const std::vector<uint8_t>& auth_resp);
  static Command encodeCommand(Command::Cmd cmd, const std::string& data, const std::string db,
                               bool is_query);
  static CommandResponse encodeCommandResponse(const std::string& data);
};
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
