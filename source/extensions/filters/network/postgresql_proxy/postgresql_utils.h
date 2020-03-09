#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

constexpr int POSTGRESQL_SUCCESS = 0;
constexpr int POSTGRESQL_FAILURE = -1;
constexpr char POSTGRESQL_STR_END = '\0';

/**
 * IO helpers for reading PostgreSQL data from a buffer.
 */
class BufferHelper : public Logger::Loggable<Logger::Id::filter> {
public:
  static bool endOfBuffer(Buffer::Instance& buffer);
  static int readUint8(Buffer::Instance& buffer, uint8_t& val);
  static int readUint16(Buffer::Instance& buffer, uint16_t& val);
  static int readUint32(Buffer::Instance& buffer, uint32_t& val);
  static int readBytes(Buffer::Instance& buffer, size_t skip_bytes);
  static int readString(Buffer::Instance& buffer, std::string& str);
  static int readStringBySize(Buffer::Instance& buffer, size_t len, std::string& str);
  static int peekUint32(Buffer::Instance& buffer, uint32_t& val);
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
