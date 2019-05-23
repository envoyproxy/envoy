#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientLogin::setClientCap(int client_cap) { client_cap_ = client_cap; }

void ClientLogin::setExtendedClientCap(int extended_client_cap) {
  extended_client_cap_ = extended_client_cap;
}

void ClientLogin::setMaxPacket(int max_packet) { max_packet_ = max_packet; }

void ClientLogin::setCharset(int charset) { charset_ = charset; }

void ClientLogin::setUsername(std::string& username) {
  if (username.length() <= MYSQL_MAX_USER_LEN) {
    username_.assign(username);
  }
}

void ClientLogin::setDb(std::string& db) { db_ = db; }

void ClientLogin::setAuthResp(std::string& auth_resp) { auth_resp_.assign(auth_resp); }

bool ClientLogin::isResponse41() const { return client_cap_ & MYSQL_CLIENT_CAPAB_41VS320; }

bool ClientLogin::isResponse320() const { return !(client_cap_ & MYSQL_CLIENT_CAPAB_41VS320); }

bool ClientLogin::isSSLRequest() const { return client_cap_ & MYSQL_CLIENT_CAPAB_SSL; }

bool ClientLogin::isConnectWithDb() const { return client_cap_ & MYSQL_CLIENT_CONNECT_WITH_DB; }

bool ClientLogin::isClientAuthLenClData() const {
  return extended_client_cap_ & MYSQL_EXT_CL_PLG_AUTH_CL_DATA;
}

bool ClientLogin::isClientSecureConnection() const {
  return extended_client_cap_ & MYSQL_EXT_CL_SECURE_CONNECTION;
}

int ClientLogin::parseMessage(Buffer::Instance& buffer, uint32_t) {
  uint16_t client_cap = 0;
  if (BufferHelper::readUint16(buffer, client_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing client_cap in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  setClientCap(client_cap);
  uint16_t extended_client_cap = 0;
  if (BufferHelper::readUint16(buffer, extended_client_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing extended_client_cap in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  setExtendedClientCap(extended_client_cap);
  uint32_t max_packet = 0;
  if (BufferHelper::readUint32(buffer, max_packet) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing max_packet in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  setMaxPacket(max_packet);
  if (isSSLRequest()) {
    // Stop Parsing if CLIENT_SSL flag is set
    return MYSQL_SUCCESS;
  }
  uint8_t charset = 0;
  if (BufferHelper::readUint8(buffer, charset) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing charset in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  setCharset(charset);
  if (BufferHelper::readBytes(buffer, UNSET_BYTES) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error skipping unset bytes in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  std::string username;
  if (BufferHelper::readString(buffer, username) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing username in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  setUsername(username);
  std::string auth_resp;
  if (isClientAuthLenClData()) {
    uint64_t auth_resp_len = 0;
    if (BufferHelper::readLengthEncodedInteger(buffer, auth_resp_len) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing LengthEncodedInteger in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readStringBySize(buffer, auth_resp_len, auth_resp) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
  } else if (isClientSecureConnection()) {
    uint8_t auth_resp_len = 0;
    if (BufferHelper::readUint8(buffer, auth_resp_len) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp_len in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readStringBySize(buffer, auth_resp_len, auth_resp) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
  } else {
    if (BufferHelper::readString(buffer, auth_resp) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
  }
  setAuthResp(auth_resp);
  if (isConnectWithDb()) {
    std::string db;
    if (BufferHelper::readString(buffer, db) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
    setDb(db);
  }
  return MYSQL_SUCCESS;
}

std::string ClientLogin::encode() {
  uint8_t enc_end_string = 0;
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  BufferHelper::addUint16(*buffer, client_cap_);
  BufferHelper::addUint16(*buffer, extended_client_cap_);
  BufferHelper::addUint32(*buffer, max_packet_);
  BufferHelper::addUint8(*buffer, charset_);
  for (int idx = 0; idx < UNSET_BYTES; idx++) {
    BufferHelper::addUint8(*buffer, 0);
  }
  BufferHelper::addString(*buffer, username_);
  BufferHelper::addUint8(*buffer, enc_end_string);
  if ((extended_client_cap_ & MYSQL_EXT_CL_PLG_AUTH_CL_DATA) ||
      (extended_client_cap_ & MYSQL_EXT_CL_SECURE_CONNECTION)) {
    BufferHelper::addUint8(*buffer, auth_resp_.length());
    BufferHelper::addString(*buffer, auth_resp_);
  } else {
    BufferHelper::addString(*buffer, auth_resp_);
    BufferHelper::addUint8(*buffer, enc_end_string);
  }
  if (client_cap_ & MYSQL_CLIENT_CONNECT_WITH_DB) {
    BufferHelper::addString(*buffer, db_);
    BufferHelper::addUint8(*buffer, enc_end_string);
  }

  return buffer->toString();
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
