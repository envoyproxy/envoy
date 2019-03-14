#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * Helpers for reading/writing ZooKeeper data from/to a buffer.
 *
 * Note: ZooKeeper's protocol uses network byte ordering (big-endian).
 */
class BufferHelper : public Logger::Loggable<Logger::Id::filter> {
public:
  static int32_t peekInt32(Buffer::Instance& buffer, uint64_t& offset);
  static int64_t peekInt64(Buffer::Instance& buffer, uint64_t& offset);
  static std::string peekString(Buffer::Instance& buffer, uint64_t& offset);
  static bool peekBool(Buffer::Instance& buffer, uint64_t& offset);
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
