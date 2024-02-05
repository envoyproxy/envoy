#include "test/mocks/buffer/mocks.h"

#include "source/common/common/assert.h"

namespace Envoy {

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase(std::function<void()> below_low,
                                                        std::function<void()> above_high,
                                                        std::function<void()> above_overflow)
    : Buffer::WatermarkBuffer(below_low, above_high, above_overflow) {}

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase()
    : Buffer::WatermarkBuffer([&]() -> void {}, [&]() -> void {}, [&]() -> void {}) {
  ASSERT(0); // This constructor is not supported for WatermarkBuffer.
}
template <>
MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(std::function<void()>, std::function<void()>,
                                                  std::function<void()>) {
  ASSERT(0); // This constructor is not supported for OwnedImpl.
}

// NOLINTNEXTLINE(modernize-use-equals-default)
template <> MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(){};

MockBufferFactory::MockBufferFactory() = default;
MockBufferFactory::~MockBufferFactory() = default;

} // namespace Envoy
