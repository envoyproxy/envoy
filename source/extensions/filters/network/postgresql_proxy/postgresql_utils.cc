#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

bool BufferHelper::endOfBuffer(Buffer::Instance& buffer) { return buffer.length() == 0; }

int BufferHelper::readUint8(Buffer::Instance& buffer, uint8_t& val) {
  try {
    val = buffer.peekLEInt<uint8_t>(0);
    buffer.drain(sizeof(uint8_t));
    return POSTGRESQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return POSTGRESQL_FAILURE;
  }
}

int BufferHelper::readUint16(Buffer::Instance& buffer, uint16_t& val) {
  try {
    val = buffer.peekLEInt<uint16_t>(0);
    buffer.drain(sizeof(uint16_t));
    return POSTGRESQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return POSTGRESQL_FAILURE;
  }
}

int BufferHelper::readUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    buffer.drain(sizeof(uint32_t));
    return POSTGRESQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return POSTGRESQL_FAILURE;
  }
}

int BufferHelper::readBytes(Buffer::Instance& buffer, size_t skip_bytes) {
  if (buffer.length() < skip_bytes) {
    return POSTGRESQL_FAILURE;
  }
  // buffer.drain(skip_bytes);
  return POSTGRESQL_SUCCESS;
}

int BufferHelper::readString(Buffer::Instance& buffer, std::string& str) {
  char end = POSTGRESQL_STR_END;
  ssize_t index = buffer.search(&end, sizeof(end), 0);
  if (index == -1) {
    return POSTGRESQL_FAILURE;
  }
  if (static_cast<int>(buffer.length()) < (index + 1)) {
    return POSTGRESQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(index)), index));
  str = str.substr(0);
  buffer.drain(index + 1);
  return POSTGRESQL_SUCCESS;
}

int BufferHelper::readStringBySize(Buffer::Instance& buffer, size_t len, std::string& str) {
  if (buffer.length() < len) {
    return POSTGRESQL_FAILURE;
  }
  str.assign(std::string(static_cast<char*>(buffer.linearize(len)), len));
  str = str.substr(0);
  buffer.drain(len);
  return POSTGRESQL_SUCCESS;
}

int BufferHelper::peekUint32(Buffer::Instance& buffer, uint32_t& val) {
  try {
    val = buffer.peekLEInt<uint32_t>(0);
    return POSTGRESQL_SUCCESS;
  } catch (EnvoyException& e) {
    // buffer underflow
    return POSTGRESQL_FAILURE;
  }
}

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
