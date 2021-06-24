#include "source/extensions/filters/network/mysql_proxy/mysql_message.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

ClientLogin MessageHelper::encodeClientLogin(AuthMethod auth_method, const std::string& username,
                                             const std::string& password, const std::string& db,
                                             const std::vector<uint8_t>& seed) {
  ClientLogin login{};
  login.setClientCap(CLIENT_SECURE_CONNECTION | CLIENT_LONG_PASSWORD | CLIENT_TRANSACTIONS |
                     CLIENT_MULTI_STATEMENTS | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  if (db.length()) {
    login.setClientCap(login.getClientCap() | CLIENT_CONNECT_WITH_DB);
  }
  login.setUsername(username);
  login.setMaxPacket(0);
  auto nseed = seed;
  if (auth_method == AuthMethod::OldPassword) {
    nseed.resize(8);
    login.setAuthResp(OldPassword::signature(password, nseed));
  } else {
    nseed.resize(20);
    login.setAuthResp(NativePassword::signature(password, nseed));
    login.setClientCap(login.getClientCap() | CLIENT_PLUGIN_AUTH | CLIENT_PROTOCOL_41);
    login.setAuthPluginName("mysql_native_password");
  }
  login.setDb(db);
  login.setCharset(DEFAULT_MYSQL_CHARSET);
  return login;
}

ClientLogin MessageHelper::encodeSslUpgrade() {
  ClientLogin login{};
  login.setClientCap(CLIENT_SSL);
  login.setMaxPacket(0);
  login.setCharset(DEFAULT_MYSQL_CHARSET);
  return login;
}

ServerGreeting MessageHelper::encodeGreeting(const std::vector<uint8_t>& seed,
                                             const std::string& auth_plugin_name) {
  ServerGreeting greet{};
  // TODO(qinggniq) note these constants at the doc
  greet.setProtocol(MYSQL_PROTOCOL_10);
  greet.setVersion("envoy-5.7");
  greet.setAuthPluginData(seed);
  greet.setThreadId(10);
  greet.setServerCharset(DEFAULT_MYSQL_CHARSET);
  greet.setServerStatus(DEFALUT_MYSQL_SERVER_STATUS);
  greet.setServerCap(CLIENT_LONG_PASSWORD | CLIENT_LONG_FLAG | CLIENT_PROTOCOL_41 |
                     CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION | CLIENT_CONNECT_WITH_DB);
  greet.setAuthPluginName(auth_plugin_name);
  return greet;
}

OkMessage MessageHelper::encodeOk() {
  OkMessage ok{};
  ok.setAffectedRows(0);
  ok.setLastInsertId(0);
  ok.setWarnings(0);
  return ok;
}

AuthSwitchMessage MessageHelper::encodeAuthSwitch(const std::vector<uint8_t>& seed) {
  return encodeAuthSwitch(seed, "mysql_native_password");
}

AuthSwitchMessage MessageHelper::encodeAuthSwitch(const std::vector<uint8_t>& seed,
                                                  const std::string& auth_plugin_name) {
  AuthSwitchMessage auth_switch{};
  auth_switch.setAuthPluginName(auth_plugin_name);
  auth_switch.setAuthPluginData(seed);
  return auth_switch;
}

AuthMoreMessage MessageHelper::encodeAuthMore(const std::vector<uint8_t>& seed) {
  AuthMoreMessage auth_more{};
  auth_more.setAuthMoreData(seed);
  return auth_more;
}

ErrMessage MessageHelper::encodeErr(uint16_t error_code, uint8_t sql_marker,
                                    std::string&& sql_state, std::string&& error_message) {
  ErrMessage resp;
  resp.setErrorCode(error_code);
  resp.setSqlStateMarker(sql_marker);
  resp.setSqlState(std::move(sql_state));
  resp.setErrorMessage(std::move(error_message));
  return resp;
}

ErrMessage MessageHelper::passwordLengthError(int len) {
  return encodeErr(ER_PASSWD_LENGTH, MYSQL_SQL_STATE_MARKER, "HY000",
                   fmt::format("Password hash should be a {}-digit hexadecimal number", len));
}

ErrMessage MessageHelper::authError(const std::string& username, const std::string& destination,
                                    bool using_password) {
  return encodeErr(ER_ACCESS_DENIED_ERROR, MYSQL_SQL_STATE_MARKER, "28000",
                   fmt::format("Access denied for user '{}'@'{}' to database 'using password: {}'",
                               username, destination, using_password ? "YES" : "NO"));
}

ErrMessage MessageHelper::dbError(const std::string& db) {
  return encodeErr(ER_ER_BAD_DB_ERROR, MYSQL_SQL_STATE_MARKER, "42000",
                   fmt::format("Unknown database {}", db));
}

ErrMessage MessageHelper::defaultError(std::string&& msg) {
  return encodeErr(ER_UNKNOWN_ERROR, MYSQL_SQL_STATE_MARKER, "HY000", std::move(msg));
}

ClientSwitchResponse MessageHelper::encodeSwithResponse(const std::vector<uint8_t>& auth_resp) {
  ClientSwitchResponse resp;
  resp.setAuthPluginResp(auth_resp);
  return resp;
}

Command MessageHelper::encodeCommand(Command::Cmd cmd, const std::string& data,
                                     const std::string db, bool is_query) {
  Command command{};
  command.setCmd(cmd);
  command.setData(data);
  command.setDb(db);
  command.setIsQuery(is_query);
  return command;
}
CommandResponse MessageHelper::encodeCommandResponse(const std::string& data) {
  CommandResponse resp{};
  resp.setData(data);
  return resp;
}
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
