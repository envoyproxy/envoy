#pragma once
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
/**
 * Message helper of MySQL, generate MySQL packs.
 */
class MessageHelper {
public:
  static ClientLogin encodeClientLogin(AuthMethod auth_method, const std::string& username,
                                       const std::string& password, const std::string& db,
                                       const std::vector<uint8_t>& seed);
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