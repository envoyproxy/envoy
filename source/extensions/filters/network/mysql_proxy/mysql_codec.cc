#include "extensions/filters/network/mysql_proxy/mysql_codec.h"

#include <arpa/inet.h>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

int MySQLCodec::BufUint8Drain(Buffer::Instance& buffer, uint8_t& val) {
  if (buffer.length() < sizeof(uint8_t)) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(0, sizeof(uint8_t), &val);
  buffer.drain(sizeof(uint8_t));
  return MYSQL_SUCCESS;
}

int MySQLCodec::BufUint16Drain(Buffer::Instance& buffer, uint16_t& val) {
  if (buffer.length() < sizeof(uint16_t)) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(0, sizeof(uint16_t), &val);
  buffer.drain(sizeof(uint16_t));
  return MYSQL_SUCCESS;
}

int MySQLCodec::BufUint32Drain(Buffer::Instance& buffer, uint32_t& val) {
  if (buffer.length() < sizeof(uint32_t)) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(0, sizeof(uint32_t), &val);
  buffer.drain(sizeof(uint32_t));
  return MYSQL_SUCCESS;
}

int MySQLCodec::BufUint64Drain(Buffer::Instance& buffer, uint64_t& val) {
  if (buffer.length() < sizeof(uint64_t)) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(0, sizeof(uint64_t), &val);
  buffer.drain(sizeof(uint64_t));
  return MYSQL_SUCCESS;
}

int MySQLCodec::BufReadBySizeDrain(Buffer::Instance& buffer, size_t len, int& val) {
  if (buffer.length() < len) {
    return MYSQL_FAILURE;
  }
  buffer.copyOut(0, len, &val);
  buffer.drain(len);
  return MYSQL_SUCCESS;
}

int MySQLCodec::DrainBytes(Buffer::Instance& buffer, size_t skip_bytes) {
  if (buffer.length() < skip_bytes) {
    return MYSQL_FAILURE;
  }
  buffer.drain(skip_bytes);
  return MYSQL_SUCCESS;
}

// Implementation of MySQL lenenc encoder based on
// https://dev.mysql.com/doc/internals/en/integer.html#packet-Protocol::LengthEncodedInteger
int MySQLCodec::ReadLengthEncodedIntegerDrain(Buffer::Instance& buffer, int& val) {
  int size = 0;
  uint8_t byte_val = 0;
  if (BufUint8Drain(buffer, byte_val) == MYSQL_FAILURE) {
    return MYSQL_FAILURE;
  }
  if (val < LENENCODINT_1BYTE) {
    val = byte_val;
    return MYSQL_SUCCESS;
  }
  if (byte_val == LENENCODINT_2BYTES) {
    size = sizeof(uint16_t);
  } else if (byte_val == LENENCODINT_3BYTES) {
    size = sizeof(uint8_t) * 3;
  } else if (byte_val == LENENCODINT_8BYTES) {
    size = sizeof(uint64_t);
  } else {
    return MYSQL_FAILURE;
  }

  if (BufReadBySizeDrain(buffer, size, val) == MYSQL_FAILURE) {
    return MYSQL_FAILURE;
  }
  return MYSQL_SUCCESS;
}

int MySQLCodec::BufStringDrain(Buffer::Instance& buffer, std::string& str) {
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

int MySQLCodec::BufStringDrainBySize(Buffer::Instance& buffer, std::string& str, size_t len) {
  if (buffer.length() < len) {
    return MYSQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(len)), len));
  str = str.substr(0);
  buffer.drain(len);
  return MYSQL_SUCCESS;
}

std::string MySQLCodec::BufToString(Buffer::Instance& buffer) {
  char* data = static_cast<char*>(buffer.linearize(buffer.length()));
  std::string s = std::string(data, buffer.length());
  return s;
}

MySQLCodec::Cmd MySQLCodec::ParseCmd(Buffer::Instance& data) {
  uint8_t cmd;
  if (BufUint8Drain(data, cmd) != MYSQL_SUCCESS) {
    return MySQLCodec::Cmd::COM_NULL;
  }
  return static_cast<MySQLCodec::Cmd>(cmd);
}

void MySQLCodec::SetSeq(int seq) { seq_ = seq; }

std::string MySQLCodec::EncodeHdr(const std::string& cmd_str, int seq) {
  MySQLCodec::MySQLHeader mysqlhdr;
  Buffer::OwnedImpl buffer;

  mysqlhdr.fields.length = cmd_str.length();
  mysqlhdr.fields.seq = seq;
  BufUint32Add(buffer, mysqlhdr.bits);
  std::string e_string = BufToString(buffer);
  e_string.append(cmd_str);
  return e_string;
}

int MySQLCodec::HdrReadDrain(Buffer::Instance& buffer, int& len, int& seq) {
  uint32_t val = 0;
  if (BufUint32Drain(buffer, val) != MYSQL_SUCCESS) {
    return MYSQL_FAILURE;
  }
  seq = htonl(val) & MYSQL_HDR_SEQ_MASK;
  len = val & MYSQL_HDR_PKT_SIZE_MASK;
  ENVOY_LOG(trace, "MYSQL-hdrseq {}, len {}", seq, len);
  return MYSQL_SUCCESS;
}

bool MySQLCodec::EndOfBuffer(Buffer::Instance& buffer) { return buffer.length() == 0; }

bool DecoderImpl::decode(Buffer::Instance& data) {
  callbacks_.decode(data);
  data.drain(data.length());
  ENVOY_LOG(trace, "{} bytes remaining after decoding", data.length());
  return false;
}

void DecoderImpl::onData(Buffer::Instance& data) {
  while (data.length() > 0 && decode(data)) {
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
