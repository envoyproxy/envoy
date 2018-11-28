#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void ServerGreeting::SetProtocol(int protocol) { protocol_ = protocol; }

void ServerGreeting::SetVersion(std::string& version) { version_.assign(version); }

void ServerGreeting::SetThreadId(int thread_id) { thread_id_ = thread_id; }

void ServerGreeting::SetSalt(std::string& salt) { salt_ = salt; }

void ServerGreeting::SetServerCap(int server_cap) { server_cap_ = server_cap; }

void ServerGreeting::SetServerLanguage(int server_language) { server_language_ = server_language; }

void ServerGreeting::SetServerStatus(int server_status) { server_status_ = server_status; }

void ServerGreeting::SetExtServerCap(int ext_server_cap) { ext_server_cap_ = ext_server_cap; }

int ServerGreeting::Decode(Buffer::Instance& buffer, int seq, int) {
  if (seq != GREETING_SEQ_NUM) {
    return MYSQL_FAILURE;
  }
  SetSeq(seq);
  uint8_t protocol = 0;
  if (BufferHelper::BufUint8Drain(buffer, protocol) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing protocol in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetProtocol(protocol);
  std::string version;
  if (BufferHelper::BufStringDrain(buffer, version) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing version in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetVersion(version);
  uint32_t thread_id = 0;
  if (BufferHelper::BufUint32Drain(buffer, thread_id) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing thread_id in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetThreadId(thread_id);
  std::string salt;
  if (BufferHelper::BufStringDrain(buffer, salt) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing salt in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetSalt(salt);
  if (protocol_ == MYSQL_PROTOCOL_9) {
    // End of HandshakeV9 greeting
    return MYSQL_SUCCESS;
  }
  uint16_t server_cap = 0;
  if (BufferHelper::BufUint16Drain(buffer, server_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_cap in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetServerCap(server_cap);
  if (BufferHelper::EndOfBuffer(buffer) == true) {
    // HandshakeV10 can terminate after Server Capabilities
    return MYSQL_SUCCESS;
  }
  uint8_t server_language = 0;
  if (BufferHelper::BufUint8Drain(buffer, server_language) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_language in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetServerLanguage(server_language);
  uint16_t server_status = 0;
  if (BufferHelper::BufUint16Drain(buffer, server_status) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing server_language in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetServerStatus(server_status);
  uint16_t ext_server_cap = 0;
  if (BufferHelper::BufUint16Drain(buffer, ext_server_cap) != MYSQL_SUCCESS) {
    ENVOY_LOG(info, "error parsing ext_server_cap in mysql Greeting msg");
    return MYSQL_FAILURE;
  }
  SetExtServerCap(ext_server_cap);
  return MYSQL_SUCCESS;
}

std::string ServerGreeting::Encode() {
  uint8_t enc_end_string = 0;
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  BufferHelper::BufUint8Add(*buffer, protocol_);
  BufferHelper::BufStringAdd(*buffer, version_);
  BufferHelper::BufUint8Add(*buffer, enc_end_string);
  BufferHelper::BufUint32Add(*buffer, thread_id_);
  BufferHelper::BufStringAdd(*buffer, salt_);
  BufferHelper::BufUint8Add(*buffer, enc_end_string);
  BufferHelper::BufUint16Add(*buffer, server_cap_);
  BufferHelper::BufUint8Add(*buffer, server_language_);
  BufferHelper::BufUint16Add(*buffer, server_status_);
  BufferHelper::BufUint16Add(*buffer, ext_server_cap_);

  std::string e_string = BufferHelper::BufToString(*buffer);
  return e_string;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
