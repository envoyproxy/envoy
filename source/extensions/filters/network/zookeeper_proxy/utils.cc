#include "source/extensions/filters/network/zookeeper_proxy/utils.h"

#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

absl::StatusOr<int32_t> BufferHelper::peekInt32(Buffer::Instance& buffer, uint64_t& offset) {
  absl::Status status = ensureMaxLen(sizeof(int32_t));
  RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(status, fmt::format("peekInt32: {}", status.message()));

  const int32_t val = buffer.peekBEInt<int32_t>(offset);
  offset += sizeof(int32_t);
  return val;
}

absl::StatusOr<int64_t> BufferHelper::peekInt64(Buffer::Instance& buffer, uint64_t& offset) {
  absl::Status status = ensureMaxLen(sizeof(int64_t));
  RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(status, fmt::format("peekInt64: {}", status.message()));

  const int64_t val = buffer.peekBEInt<int64_t>(offset);
  offset += sizeof(int64_t);
  return val;
}

absl::StatusOr<bool> BufferHelper::peekBool(Buffer::Instance& buffer, uint64_t& offset) {
  absl::Status status = ensureMaxLen(1);
  RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(status, fmt::format("peekBool: {}", status.message()));

  const char byte = buffer.peekInt<char, ByteOrder::Host, 1>(offset);
  const bool val = static_cast<bool>(byte);
  offset += 1;
  return val;
}

absl::StatusOr<std::string> BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset) {
  std::string val;
  const absl::StatusOr<int32_t> len = peekInt32(buffer, offset);
  RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(len,
                                          fmt::format("peekString: {}", len.status().message()));

  if (len.value() == 0) {
    return val;
  }

  if (buffer.length() < (offset + len.value())) {
    return absl::InvalidArgumentError("peekString: buffer is smaller than string length");
  }

  absl::Status status = ensureMaxLen(len.value());
  RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(status, fmt::format("peekString: {}", status.message()));

  std::unique_ptr<char[]> data(new char[len.value()]);
  buffer.copyOut(offset, len.value(), data.get());
  val.assign(data.get(), len.value());
  offset += len.value();

  return val;
}

void BufferHelper::skip(const uint32_t len, uint64_t& offset) {
  offset += len;
  current_ += len;
}

absl::Status BufferHelper::ensureMaxLen(const uint32_t size) {
  current_ += size;

  if (current_ > max_len_) {
    return absl::InvalidArgumentError("read beyond max length");
  }

  return absl::OkStatus();
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
