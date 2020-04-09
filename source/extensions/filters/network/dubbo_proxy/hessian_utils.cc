#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"

#include <type_traits>

#include "common/common/assert.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

namespace {

template <typename T>
typename std::enable_if<std::is_signed<T>::value, T>::type leftShift(T left, uint16_t bit_number) {
  if (left < 0) {
    left = -left;
    return -1 * (left << bit_number);
  }

  return left << bit_number;
}

inline void addByte(Buffer::Instance& buffer, const uint8_t value) { buffer.add(&value, 1); }

void addSeq(Buffer::Instance& buffer, const std::initializer_list<uint8_t>& values) {
  for (const int8_t& value : values) {
    buffer.add(&value, 1);
  }
}

size_t doWriteString(Buffer::Instance& instance, absl::string_view str_view) {
  const size_t length = str_view.length();
  constexpr size_t str_max_length = 0xffff;
  constexpr size_t two_octet_max_lenth = 1024;

  if (length < 32) {
    addByte(instance, static_cast<uint8_t>(length));
    instance.add(str_view.data(), str_view.length());
    return length + sizeof(uint8_t);
  }

  if (length < two_octet_max_lenth) {
    const uint8_t code = length >> 8; // 0x30 + length / 0x100 must less than 0x34
    const uint8_t remain = length & 0xff;
    std::initializer_list<uint8_t> values{static_cast<uint8_t>(0x30 + code), remain};
    addSeq(instance, values);
    instance.add(str_view.data(), str_view.length());
    return length + values.size();
  }

  if (length <= str_max_length) {
    const uint8_t code = length >> 8;
    const uint8_t remain = length & 0xff;
    std::initializer_list<uint8_t> values{'S', code, remain};
    addSeq(instance, values);
    instance.add(str_view.data(), str_view.length());
    return length + values.size();
  }

  std::initializer_list<uint8_t> values{0x52, 0xff, 0xff};
  addSeq(instance, values);
  instance.add(str_view.data(), str_max_length);
  const size_t size = str_max_length + values.size();
  ASSERT(size == (str_max_length + values.size()));

  const size_t child_size =
      doWriteString(instance, str_view.substr(str_max_length, length - str_max_length));
  return child_size + size;
}

} // namespace

/*
 * Reference:
 * https://cs.chromium.org/chromium/src/base/strings/string_util.h?q=WriteInto&sq=package:chromium&dr=CSs&l=426
 */
char* allocStringBuffer(std::string* str, size_t length) {
  str->reserve(length);
  str->resize(length - 1);
  return &((*str)[0]);
}

std::string HessianUtils::peekString(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  const uint8_t code = buffer.peekInt<uint8_t>(offset);
  size_t delta_length = 0;
  std::string result;
  switch (code) {
  case 0x00:
  case 0x01:
  case 0x02:
  case 0x03:
  case 0x04:
  case 0x05:
  case 0x06:
  case 0x07:
  case 0x08:
  case 0x09:
  case 0x0a:
  case 0x0b:
  case 0x0c:
  case 0x0d:
  case 0x0e:
  case 0x0f:
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x18:
  case 0x19:
  case 0x1a:
  case 0x1b:
  case 0x1c:
  case 0x1d:
  case 0x1e:
  case 0x1f:
    delta_length = code - 0x00;
    if (delta_length + 1 + offset > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    buffer.copyOut(offset + 1, delta_length, allocStringBuffer(&result, delta_length + 1));
    *size = delta_length + 1;
    return result;

  case 0x30:
  case 0x31:
  case 0x32:
  case 0x33:
    if (offset + 2 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    delta_length = (code - 0x30) * 256 + buffer.peekInt<uint8_t>(offset + 1);
    if (delta_length + 2 + offset > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    buffer.copyOut(offset + 2, delta_length, allocStringBuffer(&result, delta_length + 1));
    *size = delta_length + 2;
    return result;

  case 0x53:
    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    delta_length = buffer.peekBEInt<uint16_t>(offset + 1);

    if (delta_length + 3 + offset > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    buffer.copyOut(offset + 3, delta_length, allocStringBuffer(&result, delta_length + 1));
    *size = delta_length + 3;
    return result;

  case 0x52:
    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    delta_length = buffer.peekBEInt<uint16_t>(offset + 1);
    buffer.copyOut(offset + 3, delta_length, allocStringBuffer(&result, delta_length + 1));
    size_t next_size = 0;
    result.append(peekString(buffer, &next_size, delta_length + 3 + offset));
    *size = next_size + delta_length + 3;
    return result;
  }
  throw EnvoyException(absl::StrCat("hessian type is not string ", code));
}

std::string HessianUtils::readString(Buffer::Instance& buffer) {
  size_t size;
  std::string result(peekString(buffer, &size));
  buffer.drain(size);
  return result;
}

long HessianUtils::peekLong(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  long result;
  uint8_t code = buffer.peekInt<uint8_t>(offset);
  switch (code) {
  case 0xd8:
  case 0xd9:
  case 0xda:
  case 0xdb:
  case 0xdc:
  case 0xdd:
  case 0xde:
  case 0xdf:
  case 0xe0:
  case 0xe1:
  case 0xe2:
  case 0xe3:
  case 0xe4:
  case 0xe5:
  case 0xe6:
  case 0xe7:
  case 0xe8:
  case 0xe9:
  case 0xea:
  case 0xee:
  case 0xef:

    result = code - 0xe0;
    *size = 1;
    return result;

  case 0xf0:
  case 0xf1:
  case 0xf2:
  case 0xf3:
  case 0xf4:
  case 0xf5:
  case 0xf6:
  case 0xf7:
  case 0xf8:
  case 0xf9:
  case 0xfa:
  case 0xfb:
  case 0xfe:
  case 0xff:

    if (offset + 2 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    result = leftShift<int16_t>(code - 0xf8, 8) + buffer.peekInt<uint8_t>(offset + 1);
    *size = 2;
    return result;

  case 0x38:
  case 0x39:
  case 0x3a:
  case 0x3b:
  case 0x3c:
  case 0x3d:
  case 0x3e:
  case 0x3f:

    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    result = leftShift<int32_t>(code - 0x3c, 16) + (buffer.peekInt<uint8_t>(offset + 1) << 8) +
             buffer.peekInt<uint8_t>(offset + 2);
    *size = 3;
    return result;

  case 0x59:

    if (offset + 5 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    result = buffer.peekBEInt<uint32_t>(offset + 1);
    *size = 5;
    return result;

  case 0x4c:

    if (offset + 9 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    result = buffer.peekBEInt<int64_t>(offset + 1);
    *size = 9;
    return result;
  }

  throw EnvoyException(absl::StrCat("hessian type is not long ", code));
}

long HessianUtils::readLong(Buffer::Instance& buffer) {
  size_t size;
  const long result = peekLong(buffer, &size);
  buffer.drain(size);
  return result;
}

bool HessianUtils::peekBool(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  bool result;
  const uint8_t code = buffer.peekInt<uint8_t>(offset);
  if (code == 0x46) {
    result = false;
    *size = 1;
    return result;
  }

  if (code == 0x54) {
    result = true;
    *size = 1;
    return result;
  }

  throw EnvoyException(absl::StrCat("hessian type is not bool ", code));
}

bool HessianUtils::readBool(Buffer::Instance& buffer) {
  size_t size;
  bool result(peekBool(buffer, &size));
  buffer.drain(size);
  return result;
}

int HessianUtils::peekInt(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  const uint8_t code = buffer.peekInt<uint8_t>(offset);
  int result;

  // Compact int
  if (code >= 0x80 && code <= 0xbf) {
    result = (code - 0x90);
    *size = 1;
    return result;
  }

  switch (code) {
  case 0xc0:
  case 0xc1:
  case 0xc2:
  case 0xc3:
  case 0xc4:
  case 0xc5:
  case 0xc6:
  case 0xc7:
  case 0xc8:
  case 0xc9:
  case 0xca:
  case 0xcb:
  case 0xcd:
  case 0xce:
  case 0xcf:
    if (offset + 2 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    result = leftShift<int16_t>(code - 0xc8, 8) + buffer.peekInt<uint8_t>(offset + 1);
    *size = 2;
    return result;

  case 0xd0:
  case 0xd1:
  case 0xd2:
  case 0xd3:
  case 0xd4:
  case 0xd5:
  case 0xd6:
  case 0xd7:
    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = leftShift<int32_t>(code - 0xd4, 16) + (buffer.peekInt<uint8_t>(offset + 1) << 8) +
             buffer.peekInt<uint8_t>(offset + 2);
    *size = 3;
    return result;

  case 0x49:
    if (offset + 5 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = buffer.peekBEInt<int32_t>(offset + 1);
    *size = 5;
    return result;
  }

  throw EnvoyException(absl::StrCat("hessian type is not int ", code));
}

int HessianUtils::readInt(Buffer::Instance& buffer) {
  size_t size;
  int result(peekInt(buffer, &size));
  buffer.drain(size);
  return result;
}

double HessianUtils::peekDouble(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  double result;
  uint8_t code = buffer.peekInt<uint8_t>(offset);
  switch (code) {
  case 0x5b:
    result = 0.0;
    *size = 1;
    return result;

  case 0x5c:
    result = 1.0;
    *size = 1;
    return result;

  case 0x5d:
    if (offset + 2 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = static_cast<double>(buffer.peekInt<int8_t>(offset + 1));
    *size = 2;
    return result;

  case 0x5e:
    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = static_cast<double>(256 * buffer.peekInt<int8_t>(offset + 1) +
                                 buffer.peekInt<uint8_t>(offset + 2));
    *size = 3;
    return result;

  case 0x5f:
    if (offset + 5 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = BufferHelper::peekFloat(buffer, offset + 1);
    *size = 5;
    return result;

  case 0x44:
    if (offset + 9 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = BufferHelper::peekDouble(buffer, offset + 1);
    *size = 9;
    return result;
  }

  throw EnvoyException(absl::StrCat("hessian type is not double ", code));
}

double HessianUtils::readDouble(Buffer::Instance& buffer) {
  size_t size;
  double result(peekDouble(buffer, &size));
  buffer.drain(size);
  return result;
}

void HessianUtils::peekNull(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  uint8_t code = buffer.peekInt<uint8_t>(offset);
  if (code == 0x4e) {
    *size = 1;
    return;
  }

  throw EnvoyException(absl::StrCat("hessian type is not null ", code));
}

void HessianUtils::readNull(Buffer::Instance& buffer) {
  size_t size;
  peekNull(buffer, &size);
  buffer.drain(size);
}

std::chrono::milliseconds HessianUtils::peekDate(Buffer::Instance& buffer, size_t* size,
                                                 uint64_t offset) {
  ASSERT(buffer.length() > offset);
  std::chrono::milliseconds result;
  uint8_t code = buffer.peekInt<uint8_t>(offset);
  switch (code) {
  case 0x4b:
    if (offset + 5 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    result = std::chrono::minutes(buffer.peekBEInt<uint32_t>(offset + 1));
    *size = 5;
    return result;

  case 0x4a:
    if (offset + 9 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }
    result = std::chrono::milliseconds(buffer.peekBEInt<uint64_t>(offset + 1));
    *size = 9;
    return result;
  }

  throw EnvoyException(absl::StrCat("hessian type is not date ", code));
}

std::chrono::milliseconds HessianUtils::readDate(Buffer::Instance& buffer) {
  size_t size;
  std::chrono::milliseconds result;
  result = peekDate(buffer, &size);
  buffer.drain(size);
  return result;
}

std::string HessianUtils::peekByte(Buffer::Instance& buffer, size_t* size, uint64_t offset) {
  ASSERT(buffer.length() > offset);
  std::string result;
  uint8_t code = buffer.peekInt<uint8_t>(offset);
  size_t delta_length = 0;
  switch (code) {
  case 0x20:
  case 0x21:
  case 0x22:
  case 0x23:
  case 0x24:
  case 0x25:
  case 0x26:
  case 0x27:
  case 0x28:
  case 0x29:
  case 0x2a:
  case 0x2b:
  case 0x2c:
  case 0x2d:
  case 0x2e:
  case 0x2f:
    delta_length = code - 0x20;
    if (delta_length + 1 + offset > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    buffer.copyOut(offset + 1, delta_length, allocStringBuffer(&result, delta_length + 1));
    *size = delta_length + 1;
    return result;

  case 0x42:
    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    delta_length = buffer.peekBEInt<uint16_t>(offset + 1);
    if (delta_length + 3 + offset > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    buffer.copyOut(offset + 3, delta_length, allocStringBuffer(&result, delta_length + 1));
    *size = delta_length + 3;
    return result;

  case 0x41:
    if (offset + 3 > buffer.length()) {
      throw EnvoyException("buffer underflow");
    }

    delta_length = buffer.peekBEInt<uint16_t>(offset + 1);
    buffer.copyOut(offset + 3, delta_length, allocStringBuffer(&result, delta_length + 1));
    size_t next_size;
    result.append(peekByte(buffer, &next_size, delta_length + 3 + offset));
    *size = delta_length + 3 + next_size;
    return result;
  }

  throw EnvoyException(absl::StrCat("hessian type is not byte ", code));
}

std::string HessianUtils::readByte(Buffer::Instance& buffer) {
  size_t size;
  std::string result(peekByte(buffer, &size));
  buffer.drain(size);
  return result;
}

size_t HessianUtils::writeString(Buffer::Instance& buffer, absl::string_view str) {
  return doWriteString(buffer, str);
}

size_t HessianUtils::writeInt(Buffer::Instance& buffer, uint8_t value) {
  // Compact int
  buffer.writeByte(0x90 + value);
  return sizeof(uint8_t);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
