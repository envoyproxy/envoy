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
 * IO helpers for reading/writing ZooKeeper data from/to a buffer.
 */
class BufferHelper : public Logger::Loggable<Logger::Id::filter> {
public:
  static bool peekInt32(Buffer::Instance& buffer, uint64_t& offset, int32_t& val);
  static bool peekString(Buffer::Instance& buffer, uint64_t& offset, std::string& str);
  static bool peekBool(Buffer::Instance& buffer, uint64_t& offset, bool& val);
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
