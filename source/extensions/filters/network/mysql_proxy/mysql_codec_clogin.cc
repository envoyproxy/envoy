#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientLogin::setClientCap(uint32_t client_cap) { client_cap_ = client_cap; }

void ClientLogin::setBaseClientCap(uint16_t base_cap) {
  client_cap_ &= 0xffffffff00000000;
  client_cap_ = client_cap_ | base_cap;
}

void ClientLogin::setExtendedClientCap(uint16_t extended_client_cap) {
  uint32_t ext = extended_client_cap;
  client_cap_ &= 0x00000000ffffffff;
  client_cap_ = client_cap_ | (ext << 16);
}

void ClientLogin::setMaxPacket(uint32_t max_packet) { max_packet_ = max_packet; }

void ClientLogin::setCharset(uint8_t charset) { charset_ = charset; }

void ClientLogin::setUsername(const std::string& username) {
  if (username.length() <= MYSQL_MAX_USER_LEN) {
    username_.assign(username);
  }
}

void ClientLogin::setDb(const std::string& db) { db_ = db; }

void ClientLogin::setAuthResp(const std::string& auth_resp) { auth_resp_.assign(auth_resp); }

void ClientLogin::setAuthPluginName(const std::string& auth_plugin_name) {
  auth_plugin_name_ = auth_plugin_name;
}

bool ClientLogin::isResponse41() const { return client_cap_ & CLIENT_PROTOCOL_41; }

bool ClientLogin::isResponse320() const { return !(client_cap_ & CLIENT_PROTOCOL_41); }

bool ClientLogin::isSSLRequest() const { return client_cap_ & CLIENT_SSL; }

bool ClientLogin::isConnectWithDb() const { return client_cap_ & CLIENT_CONNECT_WITH_DB; }

bool ClientLogin::isClientAuthLenClData() const {
  return client_cap_ & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
}

bool ClientLogin::isClientSecureConnection() const {
  return client_cap_ & CLIENT_SECURE_CONNECTION;
}

DecodeStatus ClientLogin::parseMessage(Buffer::Instance& buffer, uint32_t) {
  /* 4.0 uses 2 bytes, 4.1+ uses 4 bytes, but the proto-flag is in the lower 2
   * bytes */
  uint16_t base_cap;
  if (BufferHelper::peekUint16(buffer, base_cap) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing cap client login message");
    return DecodeStatus::Failure;
  }
  setBaseClientCap(base_cap);
  if (client_cap_ & CLIENT_SSL) {
    return parseResponseSsl(buffer);
  }
  if (client_cap_ & CLIENT_PROTOCOL_41) {
    return parseResponse41(buffer);
  }
  return parseResponse320(buffer);
}

DecodeStatus ClientLogin::parseResponseSsl(Buffer::Instance& buffer) {
  if (BufferHelper::readUint32(buffer, client_cap_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing cap client ssl message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint32(buffer, max_packet_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing max packet client ssl message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, charset_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing character client ssl message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readBytes(buffer, UNSET_BYTES) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing reserved data of client ssl message");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

DecodeStatus ClientLogin::parseResponse41(Buffer::Instance& buffer) {
  if (BufferHelper::readUint32(buffer, client_cap_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing client cap of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint32(buffer, max_packet_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing max packet of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, charset_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing charset of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readBytes(buffer, UNSET_BYTES) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when skipping bytes of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readString(buffer, username_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing username of client login message");
    return DecodeStatus::Failure;
  }
  if (client_cap_ & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) {
    uint64_t auth_len;
    if (BufferHelper::readLengthEncodedInteger(buffer, auth_len) != DecodeStatus::Success) {
      ENVOY_LOG(info, "error when parsing username of client login message");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readStringBySize(buffer, auth_len, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(info, "error when parsing auth resp of client login message");
      return DecodeStatus::Failure;
    }
  } else if (client_cap_ & CLIENT_SECURE_CONNECTION) {
    uint8_t auth_len;
    if (BufferHelper::readUint8(buffer, auth_len) != DecodeStatus::Success) {
      ENVOY_LOG(info, "error when parsing auth resp length of client login message");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readStringBySize(buffer, auth_len, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(info, "error when parsing auth resp data of client login message");
      return DecodeStatus::Failure;
    }
  } else {
    if (BufferHelper::readString(buffer, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(info, "error when parsing auth resp data of client login message");
      return DecodeStatus::Failure;
    }
  }

  if ((client_cap_ & CLIENT_CONNECT_WITH_DB) &&
      (BufferHelper::readString(buffer, db_) != DecodeStatus::Success)) {
    ENVOY_LOG(info, "error when parsing db name client login message");
    return DecodeStatus::Failure;
  }
  if ((client_cap_ & CLIENT_PLUGIN_AUTH) &&
      (BufferHelper::readString(buffer, auth_plugin_name_) != DecodeStatus::Success)) {
    ENVOY_LOG(info, "error when parsing auth plugin name of client login message");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

DecodeStatus ClientLogin::parseResponse320(Buffer::Instance& buffer) {
  uint16_t base_cap;
  if (BufferHelper::readUint16(buffer, base_cap) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing cap of client login message");
    return DecodeStatus::Failure;
  }
  setBaseClientCap(base_cap);
  if (BufferHelper::readUint24(buffer, max_packet_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing max packet of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readString(buffer, username_) != DecodeStatus::Success) {
    ENVOY_LOG(info, "error when parsing username of client login message");
    return DecodeStatus::Failure;
  }
  if (client_cap_ & CLIENT_CONNECT_WITH_DB) {
    if (BufferHelper::readString(buffer, auth_resp_)) {
      ENVOY_LOG(info, "error when parsing auth resp of client login message");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readString(buffer, db_)) {
      ENVOY_LOG(info, "error when parsing db of client login message");
      return DecodeStatus::Failure;
    }
  } else {
    if (BufferHelper::readStringEof(buffer, auth_resp_)) {
      ENVOY_LOG(info, "error when parsing auth resp of client login message");
      return DecodeStatus::Failure;
    }
  }
  return DecodeStatus::Success;
}

void ClientLogin::encode(Buffer::Instance& out) {
  if (client_cap_ & CLIENT_SSL) {
    encodeResponseSsl(out);
    return;
  }
  if (client_cap_ & CLIENT_PROTOCOL_41) {
    encodeResponse41(out);
    return;
  }
  encodeResponse320(out);
}

void ClientLogin::encodeResponseSsl(Buffer::Instance& out) {
  BufferHelper::addUint32(out, client_cap_);
  BufferHelper::addUint32(out, max_packet_);
  BufferHelper::addUint8(out, charset_);
  for (int i = 0; i < UNSET_BYTES; i++) {
    BufferHelper::addUint8(out, 0);
  }
}

void ClientLogin::encodeResponse41(Buffer::Instance& out) {
  uint8_t enc_end_string = 0;
  BufferHelper::addUint32(out, client_cap_);
  BufferHelper::addUint32(out, max_packet_);
  BufferHelper::addUint8(out, charset_);
  for (int i = 0; i < UNSET_BYTES; i++) {
    BufferHelper::addUint8(out, 0);
  }
  BufferHelper::addString(out, username_);
  BufferHelper::addUint8(out, enc_end_string);
  if (client_cap_ & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) {
    BufferHelper::addLengthEncodedInteger(out, auth_resp_.length());
    BufferHelper::addString(out, auth_resp_);
  } else if (client_cap_ & CLIENT_SECURE_CONNECTION) {
    BufferHelper::addUint8(out, auth_resp_.length());
    BufferHelper::addString(out, auth_resp_);
  } else {
    BufferHelper::addString(out, auth_resp_);
    BufferHelper::addUint8(out, enc_end_string);
  }
  if (client_cap_ & CLIENT_CONNECT_WITH_DB) {
    BufferHelper::addString(out, db_);
    BufferHelper::addUint8(out, enc_end_string);
  }
  if (client_cap_ & CLIENT_PLUGIN_AUTH) {
    BufferHelper::addString(out, auth_plugin_name_);
    BufferHelper::addUint8(out, enc_end_string);
  }
}

void ClientLogin::encodeResponse320(Buffer::Instance& out) {
  uint8_t enc_end_string = 0;
  BufferHelper::addUint16(out, getBaseClientCap());
  BufferHelper::addUint24(out, max_packet_);
  BufferHelper::addString(out, username_);
  BufferHelper::addUint8(out, enc_end_string);
  if (client_cap_ & CLIENT_CONNECT_WITH_DB) {
    BufferHelper::addString(out, auth_resp_);
    BufferHelper::addUint8(out, enc_end_string);
    BufferHelper::addString(out, db_);
    BufferHelper::addUint8(out, enc_end_string);
  } else {
    BufferHelper::addString(out, auth_resp_);
    BufferHelper::addUint8(out, EOF);
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
