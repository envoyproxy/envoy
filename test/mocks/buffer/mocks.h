#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/network/io_socket_error_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"

namespace Envoy {

// A template class to allow code reuse between MockBuffer and MockWatermarkBuffer
template <class BaseClass> class MockBufferBase : public BaseClass {
public:
  MockBufferBase();
  MockBufferBase(absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
                 absl::AnyInvocable<void()> above_overflow);

  MOCK_METHOD(void, move, (Buffer::Instance & rhs));
  MOCK_METHOD(void, move, (Buffer::Instance & rhs, uint64_t length));
  MOCK_METHOD(void, move,
              (Buffer::Instance & rhs, uint64_t length, bool reset_drain_trackers_and_accounting));
  MOCK_METHOD(void, drain, (uint64_t size));

  void baseMove(Buffer::Instance& rhs) { BaseClass::move(rhs); }
  void baseDrain(uint64_t size) { BaseClass::drain(size); }

  void trackDrains(uint64_t size) {
    bytes_drained_ += size;
    BaseClass::drain(size);
  }

  uint64_t bytesDrained() const { return bytes_drained_; }

private:
  uint64_t bytes_drained_{0};
};

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase(absl::AnyInvocable<void()> below_low,
                                                        absl::AnyInvocable<void()> above_high,
                                                        absl::AnyInvocable<void()> above_overflow);
template <> MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase();

template <>
MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(absl::AnyInvocable<void()> below_low,
                                                  absl::AnyInvocable<void()> above_high,
                                                  absl::AnyInvocable<void()> above_overflow);
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

  MockWatermarkBuffer(absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
                      absl::AnyInvocable<void()> above_overflow)
      : BaseClass(std::move(below_low), std::move(above_high), std::move(above_overflow)) {
    ON_CALL(*this, move(testing::_))
        .WillByDefault(testing::Invoke(this, &MockWatermarkBuffer::baseMove));
  }
};

class MockBufferFactory : public Buffer::WatermarkFactory {
public:
  MockBufferFactory();
  ~MockBufferFactory() override;

  Buffer::InstancePtr createBuffer(absl::AnyInvocable<void()> below_low,
                                   absl::AnyInvocable<void()> above_high,
                                   absl::AnyInvocable<void()> above_overflow) override {
    auto buffer = Buffer::InstancePtr{
        createBuffer_(std::move(below_low), std::move(above_high), std::move(above_overflow))};
    ASSERT(buffer != nullptr);
    return buffer;
  }

  MOCK_METHOD(Buffer::Instance*, createBuffer_,
              (absl::AnyInvocable<void()> below_low, absl::AnyInvocable<void()> above_high,
               absl::AnyInvocable<void()> above_overflow));

  MOCK_METHOD(Buffer::BufferMemoryAccountSharedPtr, createAccount, (Http::StreamResetHandler&));
  MOCK_METHOD(uint64_t, resetAccountsGivenPressure, (float));
};

MATCHER_P(BufferEqual, rhs, testing::PrintToString(*rhs)) {
  return TestUtility::buffersEqual(arg, *rhs);
}

MATCHER_P(BufferString, m, "") {
  return testing::ExplainMatchResult(m, arg.toString(), result_listener);
}

MATCHER_P(BufferPtrString, m, "") {
  return testing::ExplainMatchResult(m, arg->toString(), result_listener);
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
