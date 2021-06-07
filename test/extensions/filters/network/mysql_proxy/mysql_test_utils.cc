#include "mysql_test_utils.h"

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

ClientLogin MessageHelper::encodeClientLogin(const std::string& username, const std::string& db,
                                             const std::vector<uint8_t>& auth_resp) {
  ClientLogin client_login{};
  client_login.setClientCap(CLIENT_SECURE_CONNECTION | CLIENT_LONG_PASSWORD | CLIENT_TRANSACTIONS |
                            CLIENT_MULTI_STATEMENTS | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA |
                            CLIENT_CONNECT_WITH_DB | CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH);
  client_login.setMaxPacket(0);
  client_login.setUsername(username);
  client_login.setClientCap(client_login.getClientCap());
  client_login.setAuthResp(auth_resp);
  client_login.setAuthPluginName("mysql_native_password");
  client_login.setDb(db);
  client_login.setCharset(DEFAULT_MYSQL_CHARSET);
  return client_login;
}

ClientLogin MessageHelper::encodeSslUpgrade() {
  ClientLogin client_login{};
  client_login.setClientCap(CLIENT_SSL);
  client_login.setMaxPacket(0);
  client_login.setCharset(DEFAULT_MYSQL_CHARSET);
  return client_login;
}

ServerGreeting MessageHelper::encodeGreeting(const std::vector<uint8_t>& seed,
                                             const std::string& auth_plugin_name) {
  ServerGreeting greet{};
  greet.setProtocol(MYSQL_PROTOCOL_10);
  greet.setVersion("5.7.6");
  greet.setAuthPluginData(seed);
  greet.setThreadId(10);
  greet.setServerCharset(DEFAULT_MYSQL_CHARSET);
  greet.setServerStatus(DEFALUT_MYSQL_SERVER_STATUS);
  greet.setServerCap(CLIENT_LONG_PASSWORD | CLIENT_LONG_FLAG | CLIENT_CONNECT_WITH_DB |
                     CLIENT_PROTOCOL_41 | CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION);
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

int MySQLTestUtils::bytesOfConnAtrributeLength(
    const std::vector<std::pair<std::string, std::string>> conn_attrs) {
  int64_t allLen = 0;
  for (const auto& kv : conn_attrs) {
    allLen += bytesOfEncodedInteger(kv.first.length());
    allLen += kv.first.length();
    allLen += bytesOfEncodedInteger(kv.second.length());
    allLen += kv.second.length();
  }
  return bytesOfEncodedInteger(allLen);
}

int MySQLTestUtils::bytesOfEncodedInteger(int64_t val) {
  if (val < 251) {
    return 1;
  } else if (val < (1 << 16)) {
    return 3;
  } else if (val < (1 << 24)) {
    return 4;
  } else {
    return 9;
  }
}

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

  ClientLoginResponse* mysql_login_resp_encode = nullptr;
  auto encodeToString = [it, seq_force, &mysql_login_resp_encode]() {
    ASSERT(mysql_login_resp_encode != nullptr);
    uint8_t seq = CHALLENGE_RESP_SEQ_NUM + 2 * it;
    if (seq_force > 0) {
      seq = seq_force;
    }
    Buffer::OwnedImpl buffer;
    mysql_login_resp_encode->encode(buffer);
    BufferHelper::encodeHdr(buffer, seq);
    return buffer.toString();
  };
  switch (srv_resp) {
  case MYSQL_RESP_OK: {
    OkMessage ok{};
    mysql_login_resp_encode = &ok;
    ok.setAffectedRows(MYSQL_SM_AFFECTED_ROWS);
    ok.setLastInsertId(MYSQL_SM_LAST_ID);
    ok.setServerStatus(MYSQL_SM_SERVER_OK);
    ok.setWarnings(MYSQL_SM_SERVER_WARNINGS);
    return encodeToString();
  }
  case MYSQL_RESP_ERR: {
    ErrMessage err{};
    mysql_login_resp_encode = &err;
    err.setErrorCode(MYSQL_ERROR_CODE);
    err.setSqlStateMarker('#');
    err.setSqlState(MySQLTestUtils::getSqlState());
    err.setErrorMessage(MySQLTestUtils::getErrorMessage());
    return encodeToString();
  }
  case MYSQL_RESP_AUTH_SWITCH: {
    AuthSwitchMessage auth_switch{};
    mysql_login_resp_encode = &auth_switch;
    auth_switch.setIsOldAuthSwitch(false);
    auth_switch.setAuthPluginData(MySQLTestUtils::getAuthPluginData20());
    auth_switch.setAuthPluginName(MySQLTestUtils::getAuthPluginName());
    return encodeToString();
  }
  case MYSQL_RESP_MORE: {
    AuthMoreMessage auth_more{};
    mysql_login_resp_encode = &auth_more;
    auth_more.setAuthMoreData(MySQLTestUtils::getAuthPluginData20());
    return encodeToString();
  }
  default: {
    AuthMoreMessage unknown{};
    mysql_login_resp_encode = &unknown;
    unknown.setRespCode(srv_resp);
    return encodeToString();
  }
  }
}

std::string MySQLTestUtils::encodeAuthSwitchResp() {
  ClientSwitchResponse mysql_switch_resp_encode{};
  mysql_switch_resp_encode.setAuthPluginResp(getAuthPluginData20());
  Buffer::OwnedImpl buffer;
  mysql_switch_resp_encode.encode(buffer);
  BufferHelper::encodeHdr(buffer, AUTH_SWITH_RESP_SEQ);
  return buffer.toString();
}

// encode message for specific packet_len
std::string MySQLTestUtils::encodeMessage(uint32_t packet_len, uint8_t it, uint8_t seq_force) {
  Buffer::OwnedImpl buffer;
  std::string res(packet_len, '0');
  buffer.add(res);
  uint8_t seq = CHALLENGE_RESP_SEQ_NUM + 2 * it;
  if (seq_force > 0) {
    seq = seq_force;
  }
  BufferHelper::encodeHdr(buffer, seq);
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
