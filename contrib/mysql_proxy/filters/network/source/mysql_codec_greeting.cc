#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"

#include "envoy/buffer/buffer.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ServerGreeting::setProtocol(uint8_t protocol) { protocol_ = protocol; }

void ServerGreeting::setVersion(const std::string& version) { version_.assign(version); }

void ServerGreeting::setThreadId(uint32_t thread_id) { thread_id_ = thread_id; }

void ServerGreeting::setAuthPluginData(const std::vector<uint8_t>& data) {
  if (data.size() <= 8) {
    auth_plugin_data1_ = data;
    return;
  }
  auth_plugin_data1_.assign(data.data(), data.data() + 8);
  auth_plugin_data2_.assign(data.data() + 8, data.data() + data.size());
}

void ServerGreeting::setAuthPluginData1(const std::vector<uint8_t>& data) {
  auth_plugin_data1_ = data;
}

void ServerGreeting::setAuthPluginData2(const std::vector<uint8_t>& data) {
  auth_plugin_data2_ = data;
}

void ServerGreeting::setServerCap(uint32_t server_cap) { server_cap_ = server_cap; }

void ServerGreeting::setBaseServerCap(uint16_t base_server_cap) {
  server_cap_ &= 0xffffffff00000000;
  server_cap_ |= base_server_cap;
}

void ServerGreeting::setExtServerCap(uint16_t ext_server_cap) {
  uint32_t ext = ext_server_cap;
  server_cap_ &= 0x00000000ffffffff;
  server_cap_ |= (ext << 16);
}

void ServerGreeting::setServerCharset(uint8_t server_charset) { server_charset_ = server_charset; }

void ServerGreeting::setServerStatus(uint16_t server_status) { server_status_ = server_status; }

void ServerGreeting::setAuthPluginName(const std::string& name) { auth_plugin_name_ = name; }

DecodeStatus ServerGreeting::parseMessage(Buffer::Instance& buffer, uint32_t) {
  // https://github.com/mysql/mysql-proxy/blob/ca6ad61af9088147a568a079c44d0d322f5bee59/src/network-mysqld-packet.c#L1171
  if (BufferHelper::readUint8(buffer, protocol_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing protocol in mysql greeting msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readString(buffer, version_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing version in mysql greeting msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::readUint32(buffer, thread_id_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing thread_id in mysql greeting msg");
    return DecodeStatus::Failure;
  }
  // read auth plugin data part 1, which is 8 byte.
  if (BufferHelper::readVectorBySize(buffer, 8, auth_plugin_data1_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing auth_plugin_data1 in mysql greeting msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::skipBytes(buffer, 1) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error skipping bytes in mysql greeting msg");

    return DecodeStatus::Failure;
  }
  if (protocol_ == MYSQL_PROTOCOL_9) {
    return DecodeStatus::Success;
  }

  uint16_t base_server_cap = 0;
  if (BufferHelper::readUint16(buffer, base_server_cap) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing cap flag[lower 2 bytes] of mysql greeting msg");
    return DecodeStatus::Failure;
  }
  setBaseServerCap(base_server_cap);

  if (BufferHelper::readUint8(buffer, server_charset_) != DecodeStatus::Success) {
    // HandshakeV10 can terminate after Server Capabilities
    return DecodeStatus::Success;
  }
  if (BufferHelper::readUint16(buffer, server_status_) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing server status of mysql greeting msg");
    return DecodeStatus::Failure;
  }
  uint16_t ext_server_cap = 0;
  if (BufferHelper::readUint16(buffer, ext_server_cap) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing cap flag[higher 2 bytes] of mysql greeting msg");
    return DecodeStatus::Failure;
  }
  setExtServerCap(ext_server_cap);
  uint8_t auth_plugin_data_len = 0;
  if (BufferHelper::readUint8(buffer, auth_plugin_data_len) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing length of auth plugin data of mysql greeting msg");
    return DecodeStatus::Failure;
  }
  if (BufferHelper::skipBytes(buffer, 10) != DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing reserved bytes of mysql greeting msg");
    return DecodeStatus::Failure;
  }
  if (server_cap_ & CLIENT_PLUGIN_AUTH) {
    int auth_plugin_data_len2 = 0;
    if (auth_plugin_data_len > 8) {
      auth_plugin_data_len2 = auth_plugin_data_len - 8;
    }
    if (BufferHelper::readVectorBySize(buffer, auth_plugin_data_len2, auth_plugin_data2_) !=
        DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth_plugin_data2 in mysql greeting msg");
      return DecodeStatus::Failure;
    }
    int skiped_bytes = 13 - (13 > auth_plugin_data_len2 ? auth_plugin_data_len2 : 13);
    if (BufferHelper::skipBytes(buffer, skiped_bytes) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when skipping bytes in mysql greeting msg");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::readString(buffer, auth_plugin_name_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth_plugin_name in mysql greeting msg");
      return DecodeStatus::Failure;
    }
  } else if (server_cap_ & CLIENT_SECURE_CONNECTION) {
    if (BufferHelper::readVectorBySize(buffer, 12, auth_plugin_data2_) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when parsing auth_plugin_data2 in mysql greeting msg");
      return DecodeStatus::Failure;
    }
    if (BufferHelper::skipBytes(buffer, 1) != DecodeStatus::Success) {
      ENVOY_LOG(debug, "error when skipping byte in mysql greeting msg");
      return DecodeStatus::Failure;
    }
  }

  // final check
  auto auth_plugin_len = auth_plugin_data1_.size() + auth_plugin_data2_.size();
  if (server_cap_ & CLIENT_PLUGIN_AUTH) {
    if (auth_plugin_len != auth_plugin_data_len) {
      ENVOY_LOG(debug, "error when final check failure of mysql greeting msg");
      return DecodeStatus::Failure;
    }
  } else if (server_cap_ & CLIENT_SECURE_CONNECTION) {
    if (auth_plugin_len != 20 && auth_plugin_data_len != 0) {
      ENVOY_LOG(debug, "error when final check failure of mysql greeting msg");
      return DecodeStatus::Failure;
    }
  }
  return DecodeStatus::Success;
}

void ServerGreeting::encode(Buffer::Instance& out) const {
  // https://github.com/mysql/mysql-proxy/blob/ca6ad61af9088147a568a079c44d0d322f5bee59/src/network-mysqld-packet.c#L1339
  uint8_t enc_end_string = 0;
  BufferHelper::addUint8(out, protocol_);
  BufferHelper::addString(out, version_);
  BufferHelper::addUint8(out, enc_end_string);
  BufferHelper::addUint32(out, thread_id_);
  BufferHelper::addVector(out, auth_plugin_data1_);
  BufferHelper::addUint8(out, enc_end_string);
  if (protocol_ == MYSQL_PROTOCOL_9) {
    return;
  }
  BufferHelper::addUint16(out, getBaseServerCap());
  BufferHelper::addUint8(out, server_charset_);
  BufferHelper::addUint16(out, server_status_);
  BufferHelper::addUint16(out, getExtServerCap());

  if (server_cap_ & CLIENT_PLUGIN_AUTH) {
    BufferHelper::addUint8(out, auth_plugin_data2_.size() + auth_plugin_data1_.size());
  } else {
    BufferHelper::addUint8(out, 0);
  }
  // reserved
  for (int i = 0; i < 10; i++) {
    BufferHelper::addUint8(out, 0);
  }
  auto auth_data = auth_plugin_data2_;
  if (server_cap_ & CLIENT_PLUGIN_AUTH) {
    auth_data.resize(13);
    BufferHelper::addVector(out, auth_data);
    BufferHelper::addString(out, auth_plugin_name_);
    BufferHelper::addUint8(out, enc_end_string);
  } else if (server_cap_ & CLIENT_SECURE_CONNECTION) {
    auth_data.resize(12);
    BufferHelper::addVector(out, auth_plugin_data2_);
    BufferHelper::addUint8(out, enc_end_string);
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
