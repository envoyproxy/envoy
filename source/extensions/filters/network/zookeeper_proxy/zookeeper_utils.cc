#include "extensions/filters/network/zookeeper_proxy/zookeeper_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

bool BufferHelper::peekInt32(Buffer::Instance& buffer, uint64_t& offset, int32_t& val) {
  try {
    val = buffer.peekBEInt<int32_t>(offset);
    offset += sizeof(uint32_t);
    return true;
  } catch (EnvoyException& e) {
    return false;
  }
}

bool BufferHelper::peekBool(Buffer::Instance& buffer, uint64_t& offset, bool& val) {
  if (buffer.length() < (offset + 1)) {
    return false;
  }

  try {
    std::string str;
    char byte = buffer.peekInt<char, ByteOrder::Host, 1>(offset);
    val = static_cast<bool>(byte & 255);
    offset += 1;
    return true;
  } catch (EnvoyException& e) {
    return false;
  }
}

bool BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset, std::string& str) {
  int32_t len = 0;
  peekInt32(buffer, offset, len);

  if (buffer.length() < (offset + len) || !len) {
    return false;
  }

  std::unique_ptr<char[]> data(new char[len]);
  buffer.copyOut(offset, len, data.get());
  str.assign(data.get(), len);
  offset += len;

  return true;
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
