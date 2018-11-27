#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientLogin::SetClientCap(int client_cap) { client_cap_ = client_cap; }

void ClientLogin::SetExtendedClientCap(int extended_client_cap) {
  extended_client_cap_ = extended_client_cap;
}

void ClientLogin::SetMaxPacket(int max_packet) { max_packet_ = max_packet; }

void ClientLogin::SetCharset(int charset) { charset_ = charset; }

void ClientLogin::SetUsername(std::string& username) {
  if (username.length() <= MYSQL_MAX_USER_LEN) {
    username_.assign(username);
  }
}

void ClientLogin::SetDB(std::string& db) { db_ = db; }

void ClientLogin::SetAuthResp(std::string& auth_resp) { auth_resp_.assign(auth_resp); }

bool ClientLogin::IsResponse41() const { return client_cap_ & MYSQL_CLIENT_CAPAB_41VS320; }

bool ClientLogin::IsResponse320() const { return !(client_cap_ & MYSQL_CLIENT_CAPAB_41VS320); }

bool ClientLogin::IsSSLRequest() const { return client_cap_ & MYSQL_CLIENT_CAPAB_SSL; }

bool ClientLogin::IsConnectWithDb() const { return client_cap_ & MYSQL_CLIENT_CONNECT_WITH_DB; }

bool ClientLogin::IsClientAuthLenClData() const {
  return extended_client_cap_ & MYSQL_EXT_CL_PLG_AUTH_CL_DATA;
}

bool ClientLogin::IsClientSecureConnection() const {
  return extended_client_cap_ & MYSQL_EXT_CL_SECURE_CONNECTION;
}

int ClientLogin::Decode(Buffer::Instance& buffer) {
  int len = 0;
  int seq = 0;
  if (BufferHelper::HdrReadDrain(buffer, len, seq) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing mysql HDR in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  if (seq != CHALLENGE_SEQ_NUM) {
    return MYSQL_FAILURE;
  }
  SetSeq(seq);
  uint16_t client_cap = 0;
  if (BufferHelper::BufUint16Drain(buffer, client_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing client_cap in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetClientCap(client_cap);
  uint16_t extended_client_cap = 0;
  if (BufferHelper::BufUint16Drain(buffer, extended_client_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing extended_client_cap in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetExtendedClientCap(extended_client_cap);
  uint32_t max_packet = 0;
  if (BufferHelper::BufUint32Drain(buffer, max_packet) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing max_packet in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetMaxPacket(max_packet);
  if (IsSSLRequest()) {
    // Stop Parsing if CLIENT_SSL flag is set
    return MYSQL_SUCCESS;
  }
  uint8_t charset = 0;
  if (BufferHelper::BufUint8Drain(buffer, charset) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing charset in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetCharset(charset);
  if (BufferHelper::DrainBytes(buffer, UNSET_BYTES) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error skipping unset bytes in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  std::string username;
  if (BufferHelper::BufStringDrain(buffer, username) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing username in mysql ClientLogin msg");
    return MYSQL_FAILURE;
  }
  SetUsername(username);
  std::string auth_resp;
  if (IsClientAuthLenClData()) {
    int auth_resp_len = 0;
    if (BufferHelper::ReadLengthEncodedIntegerDrain(buffer, auth_resp_len) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing LengthEncodedInteger in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::BufStringDrainBySize(buffer, auth_resp, auth_resp_len) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
  } else if (IsClientSecureConnection()) {
    uint8_t auth_resp_len = 0;
    if (BufferHelper::BufUint8Drain(buffer, auth_resp_len) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp_len in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::BufStringDrainBySize(buffer, auth_resp, auth_resp_len) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
  } else {
    if (BufferHelper::BufStringDrain(buffer, auth_resp) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
  }
  SetAuthResp(auth_resp);
  if (IsConnectWithDb()) {
    std::string db;
    if (BufferHelper::BufStringDrain(buffer, db) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error parsing auth_resp in mysql ClientLogin msg");
      return MYSQL_FAILURE;
    }
    SetDB(db);
  }
  return MYSQL_SUCCESS;
}

std::string ClientLogin::Encode() {
  uint8_t enc_end_string = 0;
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  BufferHelper::BufUint16Add(*buffer, client_cap_);
  BufferHelper::BufUint16Add(*buffer, extended_client_cap_);
  BufferHelper::BufUint32Add(*buffer, max_packet_);
  BufferHelper::BufUint8Add(*buffer, charset_);
  for (int idx = 0; idx < UNSET_BYTES; idx++) {
    BufferHelper::BufUint8Add(*buffer, 0);
  }
  BufferHelper::BufStringAdd(*buffer, username_);
  BufferHelper::BufUint8Add(*buffer, enc_end_string);
  if ((extended_client_cap_ & MYSQL_EXT_CL_PLG_AUTH_CL_DATA) ||
      (extended_client_cap_ & MYSQL_EXT_CL_SECURE_CONNECTION)) {
    BufferHelper::BufUint8Add(*buffer, auth_resp_.length());
    BufferHelper::BufStringAdd(*buffer, auth_resp_);
  } else {
    BufferHelper::BufStringAdd(*buffer, auth_resp_);
    BufferHelper::BufUint8Add(*buffer, enc_end_string);
  }
  if (client_cap_ & MYSQL_CLIENT_CONNECT_WITH_DB) {
    BufferHelper::BufStringAdd(*buffer, db_);
    BufferHelper::BufUint8Add(*buffer, enc_end_string);
  }

  std::string e_string = BufferHelper::BufToString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
