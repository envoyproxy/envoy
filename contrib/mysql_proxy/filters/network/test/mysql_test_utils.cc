#include "mysql_test_utils.h"

#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_switch_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

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

int MySQLTestUtils::bytesOfConnAtrributeLength(
    const std::vector<std::pair<std::string, std::string>>& conn_attrs) {
  int64_t total_len = 0;
  for (const auto& kv : conn_attrs) {
    total_len += sizeOfLengthEncodeInteger(kv.first.length());
    total_len += kv.first.length();
    total_len += sizeOfLengthEncodeInteger(kv.second.length());
    total_len += kv.second.length();
  }
  return sizeOfLengthEncodeInteger(total_len);
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
