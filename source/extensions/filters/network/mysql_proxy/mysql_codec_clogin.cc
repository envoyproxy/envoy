#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"
#include <bits/stdint-uintn.h>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ClientLogin::setClientCap(uint32_t client_cap) { client_cap_ = client_cap; }

void ClientLogin::setBaseClientCap(uint16_t base_cap) { base_cap_ = base_cap; }

void ClientLogin::setExtendedClientCap(uint16_t extended_client_cap) {
  ext_cap_ = extended_client_cap;
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

int ClientLogin::parseMessage(Buffer::Instance& buffer, uint32_t) {
  /* 4.0 uses 2 byte, 4.1+ uses 4 bytes, but the proto-flag is in the lower 2
   * bytes */
  if (BufferHelper::peekUint16(buffer, base_cap_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error when paring cap client login message");
    return MYSQL_FAILURE;
  }
  if (client_cap_ & CLIENT_SSL) {
    if (BufferHelper::readUint32(buffer, client_cap_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when paring cap client ssl message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readUint32(buffer, max_packet_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when paring max packet client ssl message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readUint8(buffer, charset_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when paring character client ssl message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readBytes(buffer, UNSET_BYTES) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when paring reserved data of client ssl message");
      return MYSQL_FAILURE;
    }
    return MYSQL_SUCCESS;
  }
  if (client_cap_ & CLIENT_PROTOCOL_41) {
    if (BufferHelper::readUint32(buffer, client_cap_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when parsing client cap of client login message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readUint32(buffer, max_packet_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when parsing max packet of client login message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readUint8(buffer, charset_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when parsing charset of client login message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readBytes(buffer, UNSET_BYTES) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when skiping bytes of client login message");
      return MYSQL_FAILURE;
    }
    if (BufferHelper::readString(buffer, username_) != MYSQL_SUCCESS) {
      ENVOY_LOG(info, "error when parsing username of client login message");
      return MYSQL_FAILURE;
    }
    if (client_cap_ & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) {
      uint64_t auth_len;
      if (BufferHelper::readLengthEncodedInteger(buffer, auth_len) != MYSQL_SUCCESS) {
        ENVOY_LOG(info, "error when parsing username of client login message");
        return MYSQL_FAILURE;
      }
      if (BufferHelper::readStringBySize(buffer, auth_len, auth_resp_) != MYSQL_SUCCESS) {
        ENVOY_LOG(info, "error when parsing auth resp of client login message");
        return MYSQL_FAILURE;
      }

    } else if (client_cap_ & CLIENT_SECURE_CONNECTION) {
      uint8_t auth_len;
      if (BufferHelper::readUint8(buffer, auth_len) != MYSQL_SUCCESS) {
        ENVOY_LOG(info, "error when parsing auth resp length of client login message");
        return MYSQL_FAILURE;
      }
      if (BufferHelper::readStringBySize(buffer, auth_len, auth_resp_) != MYSQL_SUCCESS) {
        ENVOY_LOG(info, "error when parsing auth resp data of client login message");
        return MYSQL_FAILURE;
      }
    } else {
      if (BufferHelper::readString(buffer, auth_resp_) != MYSQL_SUCCESS) {
        ENVOY_LOG(info, "error when parsing auth resp data of client login message");
        return MYSQL_FAILURE;
      }
    }

    if ((client_cap_ & CLIENT_CONNECT_WITH_DB) &&
        (BufferHelper::readString(buffer, db_) != MYSQL_SUCCESS)) {
      ENVOY_LOG(info, "error when parsing db name client login message");
      return MYSQL_FAILURE;
    }
    if ((client_cap_ & CLIENT_PLUGIN_AUTH) &&
        (BufferHelper::readString(buffer, auth_plugin_name_) != MYSQL_SUCCESS)) {
      ENVOY_LOG(info, "error when parsing auth plugin name of client login message");
      return MYSQL_FAILURE;
    }
  }

  if (BufferHelper::readUint16(buffer, base_cap_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error when parsing cap of client login message");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readUint24(buffer, max_packet_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error when paring max packet of client login message");
    return MYSQL_FAILURE;
  }
  if (BufferHelper::readString(buffer, username_) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error when paring username of client login message");
    return MYSQL_FAILURE;
  }
  // there are more
  if (BufferHelper::readStringEof(buffer, auth_resp_)) {
    ENVOY_LOG(info, "error when paring auth resp of client login message");
    return MYSQL_FAILURE;
  }
  return MYSQL_SUCCESS;
}

void ClientLogin::encode(Buffer::Instance& out) {
  uint8_t enc_end_string = 0;
  if (client_cap_ & CLIENT_SSL) {
    BufferHelper::addUint32(out, client_cap_);
    BufferHelper::addUint32(out, max_packet_);
    BufferHelper::addUint8(out, charset_);
    for (int i = 0; i < UNSET_BYTES; i++) {
      BufferHelper::addUint8(out, 0);
    }
    return;
  }
  if (client_cap_ & CLIENT_PROTOCOL_41) {
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
  } else {
    BufferHelper::addUint16(out, base_cap_);
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
      BufferHelper::addUint8(out, -1);
    }
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
