#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"

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
   * Reads an int8_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the int8_t at offset in buffer
   */
  static int8_t peekI8(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an int16_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the int16_t at offset in buffer
   */
  static int16_t peekI16(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an int32_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the int32_t at offset in buffer
   */
  static int32_t peekI32(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an int64_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the int64_t at offset in buffer
   */
  static int64_t peekI64(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an uint16_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the uint16_t at offset in buffer
   */
  static uint16_t peekU16(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an uint32_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the uint32_t at offset in buffer
   */
  static uint32_t peekU32(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads an uint64_t from the buffer at the given offset.
   * @param buffer Buffer::Instance containing data to decode
   * @param offset offset into buffer to peek at
   * @return the uint64_t at offset in buffer
   */
  static uint64_t peekU64(Buffer::Instance& buffer, uint64_t offset = 0);

  /**
   * Reads and drains an int8_t from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the int8_t at the start of buffer
   */
  static int8_t drainI8(Buffer::Instance& buffer);

  /**
   * Reads and drains an int16_t from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the int16_t at the start of buffer
   */
  static int16_t drainI16(Buffer::Instance& buffer);

  /**
   * Reads and drains an int32_t from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the int32_t at the start of buffer
   */
  static int32_t drainI32(Buffer::Instance& buffer);

  /**
   * Reads and drains an int64_t from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the int64_t at the start of buffer
   */
  static int64_t drainI64(Buffer::Instance& buffer);

  /**
   * Reads and drains an uint32_t from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the uint32_t at the start of buffer
   */
  static uint32_t drainU32(Buffer::Instance& buffer);

  /**
   * Reads and drains an uint64_t from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the uint64_t at the start of buffer
   */
  static uint64_t drainU64(Buffer::Instance& buffer);

  /**
   * Reads and drains a double from a buffer.
   * @param buffer Buffer::Instance containing data to decode
   * @return the double at the start of buffer
   */
  static double drainDouble(Buffer::Instance& buffer);

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
