#include "extensions/filters/network/zookeeper_proxy/zookeeper_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

void BufferHelper::peekInt32(Buffer::Instance& buffer, uint64_t& offset, int32_t& val) {
  val = buffer.peekBEInt<int32_t>(offset);
  offset += sizeof(int32_t);
}

void BufferHelper::peekInt64(Buffer::Instance& buffer, uint64_t& offset, int64_t& val) {
  val = buffer.peekBEInt<int64_t>(offset);
  offset += sizeof(int64_t);
}

void BufferHelper::peekBool(Buffer::Instance& buffer, uint64_t& offset, bool& val) {
  char byte = buffer.peekInt<char, ByteOrder::Host, 1>(offset);
  val = static_cast<bool>(byte & 255);
  offset += 1;
}

void BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset, std::string& str) {
  int32_t len = 0;
  peekInt32(buffer, offset, len);

  if (len == 0) {
    return;
  }

  if (buffer.length() < (offset + len)) {
    throw EnvoyException("buffer is too small");
  }

  std::unique_ptr<char[]> data(new char[len]);
  buffer.copyOut(offset, len, data.get());
  str.assign(data.get(), len);
  offset += len;
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
