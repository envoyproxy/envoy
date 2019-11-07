#include "extensions/filters/network/zookeeper_proxy/utils.h"

#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

int32_t BufferHelper::peekInt32(Buffer::Instance& buffer, uint64_t& offset) {
  ensureMaxLen(sizeof(int32_t));

  const int32_t val = buffer.peekBEInt<int32_t>(offset);
  offset += sizeof(int32_t);
  return val;
}

int64_t BufferHelper::peekInt64(Buffer::Instance& buffer, uint64_t& offset) {
  ensureMaxLen(sizeof(int64_t));

  const int64_t val = buffer.peekBEInt<int64_t>(offset);
  offset += sizeof(int64_t);
  return val;
}

bool BufferHelper::peekBool(Buffer::Instance& buffer, uint64_t& offset) {
  ensureMaxLen(1);

  const char byte = buffer.peekInt<char, ByteOrder::Host, 1>(offset);
  const bool val = static_cast<bool>(byte);
  offset += 1;
  return val;
}

std::string BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset) {
  std::string val;
  const uint32_t len = peekInt32(buffer, offset);

  if (len == 0) {
    return val;
  }

  if (buffer.length() < (offset + len)) {
    throw EnvoyException("peekString: buffer is smaller than string length");
  }

  ensureMaxLen(len);

  std::unique_ptr<char[]> data(new char[len]);
  buffer.copyOut(offset, len, data.get());
  val.assign(data.get(), len);
  offset += len;

  return val;
}

void BufferHelper::skip(const uint32_t len, uint64_t& offset) {
  offset += len;
  current_ += len;
}

void BufferHelper::ensureMaxLen(const uint32_t size) {
  current_ += size;

  if (current_ > max_len_) {
    throw EnvoyException("read beyond max length");
  }
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
