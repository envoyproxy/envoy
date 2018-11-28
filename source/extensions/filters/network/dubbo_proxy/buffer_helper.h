#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * BufferWrapper provides a partial implementation of Buffer::Instance that is sufficient for
 * BufferHelper to read protocol data without draining the buffer's contents.
 */
class BufferWrapper : public Buffer::Instance {
public:
  BufferWrapper(Buffer::Instance& underlying) : underlying_(underlying) {}

  uint64_t position() { return position_; }

  // Buffer::Instance
  void copyOut(size_t start, uint64_t size, void* data) const override {
    ASSERT(position_ + start + size <= underlying_.length());
    underlying_.copyOut(start + position_, size, data);
  }
  void drain(uint64_t size) override {
    ASSERT(position_ + size <= underlying_.length());
    position_ += size;
  }
  uint64_t length() const override {
    ASSERT(underlying_.length() >= position_);
    return underlying_.length() - position_;
  }
  void* linearize(uint32_t size) override {
    ASSERT(position_ + size <= underlying_.length());
    uint8_t* p = static_cast<uint8_t*>(underlying_.linearize(position_ + size));
    return p + position_;
  }

  std::string toString() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void add(const void*, uint64_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void addBufferFragment(Buffer::BufferFragment&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void add(absl::string_view) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void add(const Buffer::Instance&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void commit(Buffer::RawSlice*, uint64_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void prepend(absl::string_view) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void prepend(Instance&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  uint64_t getRawSlices(Buffer::RawSlice*, uint64_t) const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void move(Buffer::Instance&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void move(Buffer::Instance&, uint64_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Api::SysCallIntResult read(int, uint64_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  uint64_t reserve(uint64_t, Buffer::RawSlice*, uint64_t) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  ssize_t search(const void*, uint64_t, size_t) const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Api::SysCallIntResult write(int) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

private:
  Buffer::Instance& underlying_;
  uint64_t position_{0};
};

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
