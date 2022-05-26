#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

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

void ClientLogin::setAuthResp(const std::vector<uint8_t>& auth_resp) { auth_resp_ = auth_resp; }

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

void ClientLogin::addConnectionAttribute(const std::pair<std::string, std::string>& attr) {
  conn_attr_.emplace_back(attr);
}

DecodeStatus ClientLogin::parseMessage(Buffer::Instance& buffer, uint32_t len) {
  /* 4.0 uses 2 bytes, 4.1+ uses 4 bytes, but the proto-flag is in the lower 2
   * bytes */
  uint16_t base_cap;
  if (BufferHelper::readUint16(buffer, base_cap) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing cap flag[lower 2 byte] of client login message");
    return DecodeStatus::Failure;
  }
  setBaseClientCap(base_cap);
  if (base_cap & CLIENT_SSL) {
    return parseResponseSsl(buffer);
  }
  if (base_cap & CLIENT_PROTOCOL_41) {
    return parseResponse41(buffer);
  }
  return parseResponse320(buffer, len - sizeof(base_cap));
}

DecodeStatus ClientLogin::parseResponseSsl(Buffer::Instance& buffer) {
  uint16_t ext_cap;
  if (BufferHelper::readUint16(buffer, ext_cap) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing cap flag of client ssl message");
    return DecodeStatus::Failure;
  }
  setExtendedClientCap(ext_cap);
  if (BufferHelper::readUint32(buffer, max_packet_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing max packet length of client ssl message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, charset_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing character of client ssl message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::skipBytes(buffer, UNSET_BYTES) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing reserved bytes of client ssl message");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

DecodeStatus ClientLogin::parseResponse41(Buffer::Instance& buffer) {
  int total = buffer.length();
  uint16_t ext_cap;
  if (BufferHelper::readUint16(buffer, ext_cap) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing client cap flag of client login message");
    return DecodeStatus::Failure;
  }
  setExtendedClientCap(ext_cap);
  if (BufferHelper::readUint32(buffer, max_packet_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing max packet length of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint8(buffer, charset_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing charset of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::skipBytes(buffer, UNSET_BYTES) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when skipping bytes of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readString(buffer, username_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing username of client login message");
    return DecodeStatus::Failure;
  }
  if (client_cap_ & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) {
    uint64_t auth_len;
    if (BufferHelper::readLengthEncodedInteger(buffer, auth_len) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing length of auth response of client login message");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readVectorBySize(buffer, auth_len, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth response of client login message");
      return DecodeStatus::Failure;
    }
  } else if (client_cap_ & CLIENT_SECURE_CONNECTION) {
    uint8_t auth_len;
    if (BufferHelper::readUint8(buffer, auth_len) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing length of auth response of client login message");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readVectorBySize(buffer, auth_len, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth response of client login message");
      return DecodeStatus::Failure;
    }
  } else {
    if (BufferHelper::readVector(buffer, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth response of client login message");
      return DecodeStatus::Failure;
    }
  }

  if ((client_cap_ & CLIENT_CONNECT_WITH_DB) &&
      (BufferHelper::readString(buffer, db_) != DecodeStatus::Success)) {
    ENVOY_LOG(debug, "error when parsing db name of client login message");
    return DecodeStatus::Failure;
  }
  if ((client_cap_ & CLIENT_PLUGIN_AUTH) &&
      (BufferHelper::readString(buffer, auth_plugin_name_) != DecodeStatus::Success)) {
    ENVOY_LOG(debug, "error when parsing auth plugin name of client login message");
    return DecodeStatus::Failure;
  }
  if (client_cap_ & CLIENT_CONNECT_ATTRS) {
    // length of all key value pairs
    uint64_t kvs_len;
    if (BufferHelper::readLengthEncodedInteger(buffer, kvs_len) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing length of all key-values in connection attributes of "
                       "client login message");
      return DecodeStatus::Failure;
    }
    while (kvs_len > 0) {
      uint64_t str_len;
      uint64_t prev_len = buffer.length();
      if (BufferHelper::readLengthEncodedInteger(buffer, str_len) != DecodeStatus::Success) {
        ENVOY_LOG(debug, "error when parsing total length of connection attribute key in "
                         "connection attributes of "
                         "client login message");
        return DecodeStatus::Failure;
      }
      std::string key;
      if (BufferHelper::readStringBySize(buffer, str_len, key) != DecodeStatus::Success) {
        ENVOY_LOG(debug, "error when parsing connection attribute key in connection attributes of "
                         "client login message");
        return DecodeStatus::Failure;
      }
      if (BufferHelper::readLengthEncodedInteger(buffer, str_len) != DecodeStatus::Success) {
        ENVOY_LOG(
            debug,
            "error when parsing length of connection attribute value in connection attributes of "
            "client login message");
        return DecodeStatus::Failure;
      }
      std::string val;
      if (BufferHelper::readStringBySize(buffer, str_len, val) != DecodeStatus::Success) {
        ENVOY_LOG(debug, "error when parsing connection attribute val in connection attributes of "
                         "client login message");
        return DecodeStatus::Failure;
      }
      conn_attr_.emplace_back(std::make_pair(std::move(key), std::move(val)));
      kvs_len -= prev_len - buffer.length();
    }
  }
  ENVOY_LOG(debug, "parsed client login protocol 41, consumed len {}, remain len {}",
            total - buffer.length(), buffer.length());
  return DecodeStatus::Success;
}

DecodeStatus ClientLogin::parseResponse320(Buffer::Instance& buffer, uint32_t remain_len) {
  int origin_len = buffer.length();
  if (BufferHelper::readUint24(buffer, max_packet_) != DecodeStatus::Success) {

    ENVOY_LOG(debug, "error when parsing max packet length of client login message");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readString(buffer, username_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing username of client login message");
    return DecodeStatus::Failure;
  }
  if (client_cap_ & CLIENT_CONNECT_WITH_DB) {
    if (BufferHelper::readVector(buffer, auth_resp_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth response of client login message");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readString(buffer, db_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing db name of client login message");
      return DecodeStatus::Failure;
    }
  } else {
    int consumed_len = origin_len - buffer.length();
    if (BufferHelper::readVectorBySize(buffer, remain_len - consumed_len, auth_resp_) !=
        DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth response of client login message");
      return DecodeStatus::Failure;
    }
  }
  return DecodeStatus::Success;
}

void ClientLogin::encode(Buffer::Instance& out) const {
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

void ClientLogin::encodeResponseSsl(Buffer::Instance& out) const {
  BufferHelper::addUint32(out, client_cap_);
  BufferHelper::addUint32(out, max_packet_);
  BufferHelper::addUint8(out, charset_);
  for (int i = 0; i < UNSET_BYTES; i++) {
    BufferHelper::addUint8(out, 0);
  }
}

void ClientLogin::encodeResponse41(Buffer::Instance& out) const {
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
    BufferHelper::addLengthEncodedInteger(out, auth_resp_.size());
    BufferHelper::addVector(out, auth_resp_);
  } else if (client_cap_ & CLIENT_SECURE_CONNECTION) {
    BufferHelper::addUint8(out, auth_resp_.size());
    BufferHelper::addVector(out, auth_resp_);
  } else {
    BufferHelper::addVector(out, auth_resp_);
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
  if (client_cap_ & CLIENT_CONNECT_ATTRS) {
    Buffer::OwnedImpl conn_attr;
    for (const auto& kv : conn_attr_) {
      BufferHelper::addLengthEncodedInteger(conn_attr, kv.first.length());
      BufferHelper::addString(conn_attr, kv.first);
      BufferHelper::addLengthEncodedInteger(conn_attr, kv.second.length());
      BufferHelper::addString(conn_attr, kv.second);
    }
    BufferHelper::addLengthEncodedInteger(out, conn_attr.length());
    out.move(conn_attr);
  }
}

void ClientLogin::encodeResponse320(Buffer::Instance& out) const {
  uint8_t enc_end_string = 0;
  BufferHelper::addUint16(out, getBaseClientCap());
  BufferHelper::addUint24(out, max_packet_);
  BufferHelper::addString(out, username_);
  BufferHelper::addUint8(out, enc_end_string);
  if (client_cap_ & CLIENT_CONNECT_WITH_DB) {
    BufferHelper::addVector(out, auth_resp_);
    BufferHelper::addUint8(out, enc_end_string);
    BufferHelper::addString(out, db_);
    BufferHelper::addUint8(out, enc_end_string);
  } else {
    BufferHelper::addVector(out, auth_resp_);
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
