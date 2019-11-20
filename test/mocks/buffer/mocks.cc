#include "test/mocks/buffer/mocks.h"

#include "common/common/assert.h"

namespace Envoy {

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase(std::function<void()> below_low,
                                                        std::function<void()> above_high)
    : Buffer::WatermarkBuffer(below_low, above_high) {}

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase()
    : Buffer::WatermarkBuffer([&]() -> void {}, [&]() -> void {}) {
  ASSERT(0); // This constructor is not supported for WatermarkBuffer.
}
template <>
MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(std::function<void()>, std::function<void()>)
    : Buffer::OwnedImpl() {
  ASSERT(0); // This constructor is not supported for OwnedImpl.
}

template <> MockBufferBase<Buffer::OwnedImpl>::MockBufferBase() : Buffer::OwnedImpl() {}

MockBufferFactory::MockBufferFactory() = default;
MockBufferFactory::~MockBufferFactory() = default;

} // namespace Envoy
