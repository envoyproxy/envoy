#pragma once

#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/network/io_socket_error_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {

// A template class to allow code reuse between MockBuffer and MockWatermarkBuffer
template <class BaseClass> class MockBufferBase : public BaseClass {
public:
  MockBufferBase();
  MockBufferBase(std::function<void()> below_low, std::function<void()> above_high,
                 std::function<void()> above_overflow);

  MOCK_METHOD(void, move, (Buffer::Instance & rhs));
  MOCK_METHOD(void, move, (Buffer::Instance & rhs, uint64_t length));
  MOCK_METHOD(void, drain, (uint64_t size));

  void baseMove(Buffer::Instance& rhs) { BaseClass::move(rhs); }
  void baseDrain(uint64_t size) { BaseClass::drain(size); }

  void trackDrains(uint64_t size) {
    bytes_drained_ += size;
    BaseClass::drain(size);
  }

  uint64_t bytes_drained() const { return bytes_drained_; }

private:
  uint64_t bytes_drained_{0};
};

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase(std::function<void()> below_low,
                                                        std::function<void()> above_high,
                                                        std::function<void()> above_overflow);
template <> MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase();

template <>
MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(std::function<void()> below_low,
                                                  std::function<void()> above_high,
                                                  std::function<void()> above_overflow);
template <> MockBufferBase<Buffer::OwnedImpl>::MockBufferBase();

class MockBuffer : public MockBufferBase<Buffer::OwnedImpl> {
public:
  MockBuffer() {
    ON_CALL(*this, move(testing::_)).WillByDefault(testing::Invoke(this, &MockBuffer::baseMove));
  }
};

class MockWatermarkBuffer : public MockBufferBase<Buffer::WatermarkBuffer> {
public:
  using BaseClass = MockBufferBase<Buffer::WatermarkBuffer>;

  MockWatermarkBuffer(std::function<void()> below_low, std::function<void()> above_high,
                      std::function<void()> above_overflow)
      : BaseClass(below_low, above_high, above_overflow) {
    ON_CALL(*this, move(testing::_))
        .WillByDefault(testing::Invoke(this, &MockWatermarkBuffer::baseMove));
  }
};

class MockBufferFactory : public Buffer::WatermarkFactory {
public:
  MockBufferFactory();
  ~MockBufferFactory() override;

  Buffer::InstancePtr create(std::function<void()> below_low, std::function<void()> above_high,
                             std::function<void()> above_overflow) override {
    return Buffer::InstancePtr{create_(below_low, above_high, above_overflow)};
  }

  MOCK_METHOD(Buffer::Instance*, create_,
              (std::function<void()> below_low, std::function<void()> above_high,
               std::function<void()> above_overflow));
};

MATCHER_P(BufferEqual, rhs, testing::PrintToString(*rhs)) {
  return TestUtility::buffersEqual(arg, *rhs);
}

MATCHER_P(BufferStringEqual, rhs, rhs) {
  *result_listener << "\"" << arg.toString() << "\"";

  Buffer::OwnedImpl buffer(rhs);
  return TestUtility::buffersEqual(arg, buffer);
}

MATCHER_P(BufferStringContains, rhs,
          std::string(negation ? "doesn't contain" : "contains") + " \"" + rhs + "\"") {
  *result_listener << "\"" << arg.toString() << "\"";
  return arg.toString().find(rhs) != std::string::npos;
}

ACTION_P(AddBufferToString, target_string) {
  auto bufferToString = [](const Buffer::OwnedImpl& buf) -> std::string { return buf.toString(); };
  target_string->append(bufferToString(arg0));
  arg0.drain(arg0.length());
}

ACTION_P(AddBufferToStringWithoutDraining, target_string) {
  target_string->append(arg0.toString());
}

MATCHER_P(RawSliceVectorEqual, rhs, testing::PrintToString(rhs)) {
  return TestUtility::rawSlicesEqual(arg, rhs.data(), rhs.size());
}

} // namespace Envoy
