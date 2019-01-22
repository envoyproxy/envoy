#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

#include <arpa/inet.h>

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

std::string BufferHelper::encodeHdr(const std::string& cmd_str, int seq) {
  MySQLCodec::MySQLHeader mysqlhdr;
  mysqlhdr.fields_.length_ = cmd_str.length();
  mysqlhdr.fields_.seq_ = seq;

  Buffer::OwnedImpl buffer;
  addUint32(buffer, mysqlhdr.bits_);

  std::string e_string = buffer.toString();
  e_string.append(cmd_str);
  return e_string;
}

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer, uint64_t& offset) {
  return buffer.length() == offset;
}

int BufferHelper::peekUint8(Buffer::Instance& buffer, uint64_t& offset, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(offset);
    offset += sizeof(uint8_t);
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

int BufferHelper::peekUint16(Buffer::Instance& buffer, uint64_t& offset, uint16_t& val) {
  try {
    val = buffer.peekLEInt<uint16_t>(offset);
    offset += sizeof(uint16_t);
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

int BufferHelper::peekUint32(Buffer::Instance& buffer, uint64_t& offset, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(offset);
    offset += sizeof(uint32_t);
    return MYSQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::LengthEncodedInteger
int BufferHelper::peekLengthEncodedInteger(Buffer::Instance& buffer, uint64_t& offset,
                                           uint64_t& val) {
  uint8_t byte_val = 0;
  if (peekUint8(buffer, offset, byte_val) == MYSQL_FAILURE) {
    return MYSQL_FAILURE;
  }
  if (byte_val < LENENCODINT_1BYTE) {
    val = byte_val;
    return MYSQL_SUCCESS;
  }

  try {
    if (byte_val == LENENCODINT_2BYTES) {
      val = buffer.peekLEInt<uint64_t, sizeof(uint16_t)>(offset);
      offset += sizeof(uint16_t);
    } else if (byte_val == LENENCODINT_3BYTES) {
      val = buffer.peekLEInt<uint64_t, sizeof(uint8_t) * 3>(offset);
      offset += sizeof(uint8_t) * 3;
    } else if (byte_val == LENENCODINT_8BYTES) {
      val = buffer.peekLEInt<uint64_t>(offset);
      offset += sizeof(uint64_t);
    } else {
      return MYSQL_FAILURE;
    }
  } catch (EnvoyException& e) {
    // buffer underflow
    return MYSQL_FAILURE;
  }

  return MYSQL_SUCCESS;
}

int BufferHelper::peekBytes(Buffer::Instance& buffer, uint64_t& offset, int skip_bytes) {
  if (buffer.length() < (offset + skip_bytes)) {
    return MYSQL_FAILURE;
  }
  offset += skip_bytes;
  return MYSQL_SUCCESS;
}

int BufferHelper::peekString(Buffer::Instance& buffer, uint64_t& offset, std::string& str) {
  char end = MYSQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), offset);
  if (index == -1) {
    return MYSQL_FAILURE;
  }
  if (static_cast<int>(buffer.length()) < (index + 1)) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(index)), index));
  str = str.substr(offset);
  offset = index + 1;
  return MYSQL_SUCCESS;
}

int BufferHelper::peekStringBySize(Buffer::Instance& buffer, uint64_t& offset, int len,
                                   std::string& str) {
  if (buffer.length() < (offset + len)) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(len + offset)), len + offset));
  str = str.substr(offset);
  offset += len;
  return MYSQL_SUCCESS;
}

int BufferHelper::peekHdr(Buffer::Instance& buffer, uint64_t& offset, int& len, int& seq) {
  uint32_t val = 0;
  if (peekUint32(buffer, offset, val) != MYSQL_SUCCESS) {
    return MYSQL_FAILURE;
  }
  seq = htonl(val) & MYSQL_HDR_SEQ_MASK;
  len = val & MYSQL_HDR_PKT_SIZE_MASK;
  ENVOY_LOG(trace, "mysql_proxy: MYSQL-hdrseq {}, len {}", seq, len);
  return MYSQL_SUCCESS;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
