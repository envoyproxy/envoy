#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

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

void BufferHelper::addUint32(Buffer::Instance& buffer, uint32_t val) {
  buffer.writeLEInt<uint32_t>(val);
}

void BufferHelper::addString(Buffer::Instance& buffer, const std::string& str) { buffer.add(str); }

std::string BufferHelper::encodeHdr(const std::string& cmd_str, uint8_t seq) {
  MySQLCodec::MySQLHeader mysqlhdr;
  mysqlhdr.fields_.length_ = cmd_str.length();
  mysqlhdr.fields_.seq_ = seq;

  Buffer::OwnedImpl buffer;
  addUint32(buffer, mysqlhdr.bits_);

  std::string e_string = buffer.toString();
  e_string.append(cmd_str);
  return e_string;
}

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer) { return buffer.length() == 0; }

int BufferHelper::readUint8(Buffer::Instance& buffer, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(0);
    buffer.drain(sizeof(uint8_t));
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

int BufferHelper::readUint16(Buffer::Instance& buffer, uint16_t& val) {
  try {
    val = buffer.peekLEInt<uint16_t>(0);
    buffer.drain(sizeof(uint16_t));
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

int BufferHelper::readUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    buffer.drain(sizeof(uint32_t));
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::LengthEncodedInteger
int BufferHelper::readLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& val) {
  uint8_t byte_val = 0;
  if (readUint8(buffer, byte_val) == MYSQL_FAILURE) {
    return MYSQL_FAILURE;
  }
  if (byte_val < LENENCODINT_1BYTE) {
    val = byte_val;
    return MYSQL_SUCCESS;
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
      return MYSQL_FAILURE;
    }
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }

  return MYSQL_SUCCESS;
}

int BufferHelper::readBytes(Buffer::Instance& buffer, size_t skip_bytes) {
  if (buffer.length() < skip_bytes) {
    return MYSQL_FAILURE;
  }
  buffer.drain(skip_bytes);
  return MYSQL_SUCCESS;
}

int BufferHelper::readString(Buffer::Instance& buffer, std::string& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), 0);
  if (index == -1) {
    return MYSQL_FAILURE;
  }
  if (static_cast<int>(buffer.length()) < (index + 1)) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(index)), index));
  str = str.substr(0);
  buffer.drain(index + 1);
  return MYSQL_SUCCESS;
}

int BufferHelper::readStringBySize(Buffer::Instance& buffer, size_t len, std::string& str) {
  if (buffer.length() < len) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(len)), len));
  str = str.substr(0);
  buffer.drain(len);
  return MYSQL_SUCCESS;
}

int BufferHelper::peekUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

void BufferHelper::consumeHdr(Buffer::Instance& buffer) { buffer.drain(sizeof(uint32_t)); }

int BufferHelper::peekHdr(Buffer::Instance& buffer, uint32_t& len, uint8_t& seq) {
  uint32_t val = 0;
  if (peekUint32(buffer, val) != MYSQL_SUCCESS) {
    return MYSQL_FAILURE;
  }
  seq = htobe32(val) & MYSQL_HDR_SEQ_MASK;
  len = val & MYSQL_HDR_PKT_SIZE_MASK;
  ENVOY_LOG(trace, "mysql_proxy: MYSQL-hdrseq {}, len {}", seq, len);
  return MYSQL_SUCCESS;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
