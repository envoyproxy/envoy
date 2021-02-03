#include "extensions/filters/network/postgres_proxy/postgres_message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// String type methods.
bool String::read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
  // First find the terminating zero.
  const char zero = 0;
  const ssize_t index = data.search(&zero, 1, pos);
  if (index == -1) {
    return false;
  }

  // Reserve that many bytes in the string.
  const uint64_t size = index - pos;
  value_.resize(size);
  // Now copy from buffer to string.
  data.copyOut(pos, index - pos, value_.data());
  pos += (size + 1);
  left -= (size + 1);

  return true;
}

std::string String::toString() const { return absl::StrCat("[", value_, "]"); }

// ByteN type methods.
bool ByteN::read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
  if (left > (data.length() - pos)) {
    return false;
  }
  value_.resize(left);
  data.copyOut(pos, left, value_.data());
  pos += left;
  left = 0;
  return true;
}

std::string ByteN::toString() const {
  std::string out = "[";
  absl::StrAppend(&out, absl::StrJoin(value_, " "));
  absl::StrAppend(&out, "]");
  return out;
}

// VarByteN type methods.
bool VarByteN::read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
  if ((left < sizeof(int32_t)) || ((data.length() - pos) < sizeof(int32_t))) {
    return false;
  }
  len_ = data.peekBEInt<int32_t>(pos);
  pos += sizeof(int32_t);
  left -= sizeof(int32_t);
  if (len_ < 1) {
    // There is no payload if length is not positive.
    value_.clear();
    return true;
  }
  if ((left < static_cast<uint64_t>(len_)) ||
      ((data.length() - pos) < static_cast<uint64_t>(len_))) {
    return false;
  }
  value_.resize(len_);
  data.copyOut(pos, len_, value_.data());
  pos += len_;
  left -= len_;
  return true;
}

std::string VarByteN::toString() const {
  std::string out;
  out = fmt::format("[({} bytes):", len_);
  absl::StrAppend(&out, absl::StrJoin(value_, " "));
  absl::StrAppend(&out, "]");
  return out;
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
