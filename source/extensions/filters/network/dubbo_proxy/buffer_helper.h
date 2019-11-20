#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * BufferHelper provides buffer operations for reading bytes and numbers in the various encodings
 * used by protocols.
 */
class BufferHelper {
public:
  /**
   * Reads an double from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the double at offset in buffer
   */
  static double peekDouble(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an float from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the float at offset in buffer
   */
  static float peekFloat(Buffer::Instance& buffer, uint64_t offset = 0);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
