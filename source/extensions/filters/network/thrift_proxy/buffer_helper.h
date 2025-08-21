#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * BufferHelper provides buffer operations for reading bytes and numbers in the various encodings
 * used by Thrift protocols.
 */
class BufferHelper {
public:
  /**
   * Reads and drains a double from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the double at the start of buffer
   */
  static double drainBEDouble(Buffer::Instance& buffer);

  /**
   * Peeks at a variable-length int32_t at offset. Updates size to the number of bytes used to
   * encode the value. If insufficient bytes are available in the buffer to complete decoding, size
   * is set to a negative number whose absolute value is the number of bytes examined. At least one
   * byte must be available in the buffer.
   *
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @param size updated with number of bytes decoded for successful result (positive values of
   *        size) or the number of bytes examined before underflowing (negative values of size).
   * @return the decoded variable-length int32_t (if size > 0), otherwise 0.
   * @throw EnvoyException if there is a buffer underflow, but more data would result in an integer
   *                       larger than 32 bits.
   */
  static int32_t peekVarIntI32(Buffer::Instance& buffer, uint64_t offset, int& size);

  /**
   * Peeks at the zig-zag encoded int64_t at offset. Updates size with the same semantics as
   * peekVarIntI32.
   *
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @param size updated with number of bytes decoded for successful result (positive values of
   *        size) or the number of bytes examined before underflowing (negative values of size).
   * @return the decoded variable-length zig-zag encoded int64_t (if size > 0), otherwise 0.
   * @throw EnvoyException if there is a buffer underflow, but more data would result in an integer
   *                       larger than 64 bits.
   */
  static int64_t peekZigZagI64(Buffer::Instance& buffer, uint64_t offset, int& size);

  /**
   * Peeks at the zig-zag encoded int32_t at offset with the same semantics for the size parameter
   * as peekVarInt32.
   *
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @param size updated with number of bytes decoded for successful result (positive values of
   *        size) or the number of bytes examined before underflowing (negative values of size).
   * @return the decoded variable-length zig-zag encoded int32_t (if size > 0), otherwise 0.
   * @throw EnvoyException if there is a buffer underflow, but more data would result in an integer
   *                       larger than 32 bits.
   */
  static int32_t peekZigZagI32(Buffer::Instance& buffer, uint64_t offset, int& size);

  /**
   * Writes a double to the buffer.
   * @param buffer Buffer::Instance written to
   * @param value the double to write
   */
  static void writeBEDouble(Buffer::Instance& buffer, double value);

  /**
   * Writes a var-int encoded int32_t to the buffer.
   * @param buffer Buffer::Instance written to
   * @param value the int32_t to write
   */
  static void writeVarIntI32(Buffer::Instance& buffer, int32_t value);

  /**
   * Writes a var-int encoded int64_t to the buffer.
   * @param buffer Buffer::Instance written to
   * @param value the int64_t to write
   */
  static void writeVarIntI64(Buffer::Instance& buffer, int64_t value);

  /**
   * Writes a zig-zag encoded int32_t to the buffer.
   * @param buffer Buffer::Instance written to
   * @param value the int32_t to write
   */
  static void writeZigZagI32(Buffer::Instance& buffer, int32_t value);

  /**
   * Writes a zig-zag encoded int64_t to the buffer.
   * @param buffer Buffer::Instance written to
   * @param value the int64_t to write
   */
  static void writeZigZagI64(Buffer::Instance& buffer, int64_t value);

private:
  /**
   * Peeks at a variable-length int of up to 64 bits at offset. Updates size to indicate how many
   * bytes were examined.
   *
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @param size updated with number of bytes decoded for successful result (positive values of
   *        size) or the number of bytes examined before underflowing (negative values of size).
   * @return the decoded variable-length int64_t (if size > 0), otherwise 0.
   */
  static uint64_t peekVarInt(Buffer::Instance& buffer, uint64_t offset, int& size);
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
