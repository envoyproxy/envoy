#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ServerGreeting::setProtocol(int protocol) { protocol_ = protocol; }

void ServerGreeting::setVersion(std::string& version) { version_.assign(version); }

void ServerGreeting::setThreadId(int thread_id) { thread_id_ = thread_id; }

void ServerGreeting::setSalt(std::string& salt) { salt_ = salt; }

void ServerGreeting::setServerCap(int server_cap) { server_cap_ = server_cap; }

void ServerGreeting::setServerLanguage(int server_language) { server_language_ = server_language; }

void ServerGreeting::setServerStatus(int server_status) { server_status_ = server_status; }

void ServerGreeting::setExtServerCap(int ext_server_cap) { ext_server_cap_ = ext_server_cap; }

int ServerGreeting::parseMessage(Buffer::Instance& buffer, uint32_t) {
  uint8_t protocol = 0;
  if (BufferHelper::readUint8(buffer, protocol) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing protocol in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setProtocol(protocol);
  std::string version;
  if (BufferHelper::readString(buffer, version) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing version in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setVersion(version);
  uint32_t thread_id = 0;
  if (BufferHelper::readUint32(buffer, thread_id) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing thread_id in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setThreadId(thread_id);
  std::string salt;
  if (BufferHelper::readString(buffer, salt) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing salt in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setSalt(salt);
  if (protocol_ == MYSQL_PROTOCOL_9) {
    // End of HandshakeV9 greeting
    return MYSQL_SUCCESS;
  }
  uint16_t server_cap = 0;
  if (BufferHelper::readUint16(buffer, server_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_cap in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setServerCap(server_cap);
  if (BufferHelper::endOfBuffer(buffer)) {
    // HandshakeV10 can terminate after Server Capabilities
    return MYSQL_SUCCESS;
  }
  uint8_t server_language = 0;
  if (BufferHelper::readUint8(buffer, server_language) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_language in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setServerLanguage(server_language);
  uint16_t server_status = 0;
  if (BufferHelper::readUint16(buffer, server_status) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_status in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setServerStatus(server_status);
  uint16_t ext_server_cap = 0;
  if (BufferHelper::readUint16(buffer, ext_server_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing ext_server_cap in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  setExtServerCap(ext_server_cap);
  return MYSQL_SUCCESS;
}

std::string ServerGreeting::encode() {
  uint8_t enc_end_string = 0;
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  BufferHelper::addUint8(*buffer, protocol_);
  BufferHelper::addString(*buffer, version_);
  BufferHelper::addUint8(*buffer, enc_end_string);
  BufferHelper::addUint32(*buffer, thread_id_);
  BufferHelper::addString(*buffer, salt_);
  BufferHelper::addUint8(*buffer, enc_end_string);
  BufferHelper::addUint16(*buffer, server_cap_);
  BufferHelper::addUint8(*buffer, server_language_);
  BufferHelper::addUint16(*buffer, server_status_);
  BufferHelper::addUint16(*buffer, ext_server_cap_);

  return buffer->toString();
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
