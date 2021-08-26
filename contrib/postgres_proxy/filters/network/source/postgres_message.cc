#include "contrib/postgres_proxy/filters/network/source/postgres_message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// String type methods.
bool String::read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
  // read method uses values set by validate method.
  // This avoids unnecessary repetition of scanning data looking for terminating zero.
  ASSERT(pos == start_);
  ASSERT(end_ >= start_);

  // Reserve that many bytes in the string.
  const uint64_t size = end_ - start_;
  value_.resize(size);
  // Now copy from buffer to string.
  data.copyOut(pos, size, value_.data());
  pos += (size + 1);
  left -= (size + 1);

  return true;
}

std::string String::toString() const { return absl::StrCat("[", value_, "]"); }

Message::ValidationResult String::validate(const Buffer::Instance& data,
                                           const uint64_t start_offset, uint64_t& pos,
                                           uint64_t& left) {
  // Try to find the terminating zero.
  // If found, all is good. If not found, we may need more data.
  const char zero = 0;
  const ssize_t index = data.search(&zero, 1, pos);
  if (index == -1) {
    if (left <= (data.length() - pos)) {
      // Message ended before finding terminating zero.
      return Message::ValidationFailed;
    } else {
      return Message::ValidationNeedMoreData;
    }
  }
  // Found, but after the message boundary.
  const uint64_t size = index - pos;
  if (size >= left) {
    return Message::ValidationFailed;
  }

  start_ = pos - start_offset;
  end_ = start_ + size;

  pos += (size + 1);
  left -= (size + 1);
  return Message::ValidationOK;
}

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
// Since ByteN does not have a length field, it is not possible to verify
// its correctness.
Message::ValidationResult ByteN::validate(const Buffer::Instance& data, const uint64_t,
                                          uint64_t& pos, uint64_t& left) {
  if (left > (data.length() - pos)) {
    return Message::ValidationNeedMoreData;
  }

  pos += left;
  left = 0;

  return Message::ValidationOK;
}

std::string ByteN::toString() const {
  std::string out = "[";
  absl::StrAppend(&out, absl::StrJoin(value_, " "));
  absl::StrAppend(&out, "]");
  return out;
}

// VarByteN type methods.
bool VarByteN::read(const Buffer::Instance& data, uint64_t& pos, uint64_t& left) {
  // len_ was set by validator, skip it.
  pos += sizeof(int32_t);
  left -= sizeof(int32_t);
  if (len_ < 1) {
    // There is no payload if length is not positive.
    value_.clear();
    return true;
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

Message::ValidationResult VarByteN::validate(const Buffer::Instance& data, const uint64_t,
                                             uint64_t& pos, uint64_t& left) {
  if (left < sizeof(int32_t)) {
    // Malformed message.
    return Message::ValidationFailed;
  }

  if ((data.length() - pos) < sizeof(int32_t)) {
    return Message::ValidationNeedMoreData;
  }

  // Read length of the VarByteN structure.
  len_ = data.peekBEInt<int32_t>(pos);
  if (static_cast<int64_t>(len_) > static_cast<int64_t>(left)) {
    // VarByteN would extend past the current message boundaries.
    // Lengths of message and individual fields do not match.
    return Message::ValidationFailed;
  }

  if (len_ < 1) {
    // There is no payload if length is not positive.
    pos += sizeof(int32_t);
    left -= sizeof(int32_t);
    return Message::ValidationOK;
  }

  if ((data.length() - pos) < (len_ + sizeof(int32_t))) {
    return Message::ValidationNeedMoreData;
  }

  pos += (len_ + sizeof(int32_t));
  left -= (len_ + sizeof(int32_t));

  return Message::ValidationOK;
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
