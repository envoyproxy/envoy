#pragma once

#include <chrono>
#include <map>
#include <string>

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/*
 * Hessian deserialization
 * See http://hessian.caucho.com/doc/hessian-serialization.html
 */
class HessianUtils {
public:
  static std::string peekString(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);
  static long peekLong(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);
  static bool peekBool(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);
  static int peekInt(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);
  static double peekDouble(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);
  static void peekNull(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);
  static std::chrono::milliseconds peekDate(Buffer::Instance& buffer, size_t* size,
                                            uint64_t offset = 0);
  static std::string peekByte(Buffer::Instance& buffer, size_t* size, uint64_t offset = 0);

  static std::string readString(Buffer::Instance& buffer);
  static long readLong(Buffer::Instance& buffer);
  static bool readBool(Buffer::Instance& buffer);
  static int readInt(Buffer::Instance& buffer);
  static double readDouble(Buffer::Instance& buffer);
  static void readNull(Buffer::Instance& buffer);
  static std::chrono::milliseconds readDate(Buffer::Instance& buffer);
  static std::string readByte(Buffer::Instance& buffer);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
