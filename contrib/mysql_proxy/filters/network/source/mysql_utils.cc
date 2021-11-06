#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

void BufferHelper::addUint8(Buffer::Instance& buffer, uint8_t val) {
  buffer.writeLEInt<uint8_t>(val);
}

void BufferHelper::addUint16(Buffer::Instance& buffer, uint16_t val) {
  buffer.writeLEInt<uint16_t>(val);
}

void BufferHelper::addUint24(Buffer::Instance& buffer, uint32_t val) {
  buffer.writeLEInt<uint32_t, sizeof(uint8_t) * 3>(val);
}

void BufferHelper::addUint32(Buffer::Instance& buffer, uint32_t val) {
  buffer.writeLEInt<uint32_t>(val);
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::FixedLengthInteger
void BufferHelper::addLengthEncodedInteger(Buffer::Instance& buffer, uint64_t val) {
  if (val < 251) {
    buffer.writeLEInt<uint8_t>(val);
  } else if (val < (1 << 16)) {
    buffer.writeLEInt<uint8_t>(0xfc);
    buffer.writeLEInt<uint16_t>(val);
  } else if (val < (1 << 24)) {
    buffer.writeLEInt<uint8_t>(0xfd);
    buffer.writeLEInt<uint64_t, sizeof(uint8_t) * 3>(val);
  } else {
    buffer.writeLEInt<uint8_t>(0xfe);
    buffer.writeLEInt<uint64_t>(val);
  }
}

void BufferHelper::addBytes(Buffer::Instance& buffer, const char* str, int size) {
  buffer.add(str, size);
}

void BufferHelper::encodeHdr(Buffer::Instance& pkg, uint8_t seq) {
  // the pkg buffer should only contain one package data
  uint32_t header = (seq << 24) | (pkg.length() & MYSQL_HDR_PKT_SIZE_MASK);
  Buffer::OwnedImpl buffer;
  addUint32(buffer, header);
  pkg.prepend(buffer);
}

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer) { return buffer.length() == 0; }

DecodeStatus BufferHelper::readUint8(Buffer::Instance& buffer, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(0);
    buffer.drain(sizeof(uint8_t));
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readUint16(Buffer::Instance& buffer, uint16_t& val) {
  try {
    val = buffer.peekLEInt<uint16_t>(0);
    buffer.drain(sizeof(uint16_t));
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readUint24(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t, sizeof(uint8_t) * 3>(0);
    buffer.drain(sizeof(uint8_t) * 3);
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::readUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    buffer.drain(sizeof(uint32_t));
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::LengthEncodedInteger
DecodeStatus BufferHelper::readLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& val) {
  uint8_t byte_val = 0;
  if (readUint8(buffer, byte_val) == DecodeStatus::Failure) {
    return DecodeStatus::Failure;
  }
  if (byte_val < LENENCODINT_1BYTE) {
    val = byte_val;
    return DecodeStatus::Success;
  }

  try {
    if (byte_val == LENENCODINT_2BYTES) {
      val = buffer.peekLEInt<uint64_t, sizeof(uint16_t)>(0);
      buffer.drain(sizeof(uint16_t));
    } else if (byte_val == LENENCODINT_3BYTES) {
      val = buffer.peekLEInt<uint64_t, sizeof(uint8_t) * 3>(0);
      buffer.drain(sizeof(uint8_t) * 3);
    } else if (byte_val == LENENCODINT_8BYTES) {
      val = buffer.peekLEInt<uint64_t>(0);
      buffer.drain(sizeof(uint64_t));
    } else {
      return DecodeStatus::Failure;
    }
  } catch (EnvoyException& e) {
    // buffer underflow
    return DecodeStatus::Failure;
  }

  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::skipBytes(Buffer::Instance& buffer, size_t skip_bytes) {
  if (buffer.length() < skip_bytes) {
    return DecodeStatus::Failure;
  }
  buffer.drain(skip_bytes);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::readString(Buffer::Instance& buffer, std::string& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), 0);
  if (index == -1) {
    return DecodeStatus::Failure;
  }
  str.assign(static_cast<char*>(buffer.linearize(index)), index);
  buffer.drain(index + 1);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::readVector(Buffer::Instance& buffer, std::vector<uint8_t>& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), 0);
  if (index == -1) {
    return DecodeStatus::Failure;
  }
  auto arr = reinterpret_cast<uint8_t*>(buffer.linearize(index));
  str.assign(arr, arr + index);
  buffer.drain(index + 1);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::readStringBySize(Buffer::Instance& buffer, size_t len,
                                            std::string& str) {
  if (buffer.length() < len) {
    return DecodeStatus::Failure;
  }
  str.assign(static_cast<char*>(buffer.linearize(len)), len);
  buffer.drain(len);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::readVectorBySize(Buffer::Instance& buffer, size_t len,
                                            std::vector<uint8_t>& data) {
  if (buffer.length() < len) {
    return DecodeStatus::Failure;
  }
  uint8_t* arr = reinterpret_cast<uint8_t*>(buffer.linearize(len));
  data.assign(&arr[0], &arr[len]);
  buffer.drain(len);
  return DecodeStatus::Success;
}

DecodeStatus BufferHelper::peekUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    return DecodeStatus::Failure;
  }
}

DecodeStatus BufferHelper::peekUint8(Buffer::Instance& buffer, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(0);
    return DecodeStatus::Success;
  } catch (EnvoyException& e) {
    return DecodeStatus::Failure;
  }
}

void BufferHelper::consumeHdr(Buffer::Instance& buffer) { buffer.drain(sizeof(uint32_t)); }

DecodeStatus BufferHelper::peekHdr(Buffer::Instance& buffer, uint32_t& len, uint8_t& seq) {
  uint32_t val = 0;
  if (peekUint32(buffer, val) != DecodeStatus::Success) {
    return DecodeStatus::Failure;
  }
  seq = htobe32(val) & MYSQL_HDR_SEQ_MASK;
  len = val & MYSQL_HDR_PKT_SIZE_MASK;
  ENVOY_LOG(trace, "mysql_proxy: MYSQL-hdrseq {}, len {}", seq, len);
  return DecodeStatus::Success;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
