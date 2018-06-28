#pragma once

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {

// A template class to allow code reuse between MockBuffer and MockWatermarkBuffer
template <class BaseClass> class MockBufferBase : public BaseClass {
public:
  MockBufferBase();
  MockBufferBase(std::function<void()> below_low, std::function<void()> above_high);

  MOCK_METHOD1(write, int(int fd));
  MOCK_METHOD1(move, void(Buffer::Instance& rhs));
  MOCK_METHOD2(move, void(Buffer::Instance& rhs, uint64_t length));
  MOCK_METHOD1(drain, void(uint64_t size));

  void baseMove(Buffer::Instance& rhs) { BaseClass::move(rhs); }
  void baseDrain(uint64_t size) { BaseClass::drain(size); }

  int trackWrites(int fd) {
    int bytes_written = BaseClass::write(fd);
    if (bytes_written > 0) {
      bytes_written_ += bytes_written;
    }
    return bytes_written;
  }

  void trackDrains(uint64_t size) {
    bytes_drained_ += size;
    BaseClass::drain(size);
  }

  // A convenience function to invoke on write() which fails the write with EAGAIN.
  int failWrite(int) {
    errno = EAGAIN;
    return -1;
  }

  int bytes_written() const { return bytes_written_; }
  uint64_t bytes_drained() const { return bytes_drained_; }

private:
  int bytes_written_{0};
  uint64_t bytes_drained_{0};
};

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase(std::function<void()> below_low,
                                                        std::function<void()> above_high);
template <> MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase();

template <>
MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(std::function<void()> below_low,
                                                  std::function<void()> above_high);
template <> MockBufferBase<Buffer::OwnedImpl>::MockBufferBase();

class MockBuffer : public MockBufferBase<Buffer::OwnedImpl> {
public:
  MockBuffer() {
    ON_CALL(*this, write(testing::_))
        .WillByDefault(testing::Invoke(this, &MockBuffer::trackWrites));
    ON_CALL(*this, move(testing::_)).WillByDefault(testing::Invoke(this, &MockBuffer::baseMove));
  }
};

class MockWatermarkBuffer : public MockBufferBase<Buffer::WatermarkBuffer> {
public:
  typedef MockBufferBase<Buffer::WatermarkBuffer> BaseClass;

  MockWatermarkBuffer(std::function<void()> below_low, std::function<void()> above_high)
      : BaseClass(below_low, above_high) {
    ON_CALL(*this, write(testing::_))
        .WillByDefault(testing::Invoke(this, &MockWatermarkBuffer::trackWrites));
    ON_CALL(*this, move(testing::_))
        .WillByDefault(testing::Invoke(this, &MockWatermarkBuffer::baseMove));
  }
};

class MockBufferFactory : public Buffer::WatermarkFactory {
public:
  Buffer::InstancePtr create(std::function<void()> below_low,
                             std::function<void()> above_high) override {
    return Buffer::InstancePtr{create_(below_low, above_high)};
  }

  MOCK_METHOD2(create_, Buffer::Instance*(std::function<void()> below_low,
                                          std::function<void()> above_high));
};

MATCHER_P(BufferEqual, rhs, testing::PrintToString(*rhs)) {
  return TestUtility::buffersEqual(arg, *rhs);
}

MATCHER_P(BufferStringEqual, rhs, rhs) {
  *result_listener << "\"" << arg.toString() << "\"";

  Buffer::OwnedImpl buffer(rhs);
  return TestUtility::buffersEqual(arg, buffer);
}

ACTION_P(AddBufferToString, target_string) {
  auto bufferToString = [](const Buffer::OwnedImpl& buf) -> std::string { return buf.toString(); };
  target_string->append(bufferToString(arg0));
  arg0.drain(arg0.length());
}

ACTION_P(AddBufferToStringWithoutDraining, target_string) {
  target_string->append(arg0.toString());
}

} // namespace Envoy
