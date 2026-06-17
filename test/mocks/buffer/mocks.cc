#include "test/mocks/buffer/mocks.h"

#include "source/common/common/assert.h"

namespace Envoy {

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase(absl::AnyInvocable<void()> below_low,
                                                        absl::AnyInvocable<void()> above_high,
                                                        absl::AnyInvocable<void()> above_overflow)
    : Buffer::WatermarkBuffer(std::move(below_low), std::move(above_high),
                              std::move(above_overflow)) {}

template <>
MockBufferBase<Buffer::WatermarkBuffer>::MockBufferBase()
    : Buffer::WatermarkBuffer([&]() -> void {}, [&]() -> void {}, [&]() -> void {}) {
  ASSERT(0); // This constructor is not supported for WatermarkBuffer.
}
template <>
MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(absl::AnyInvocable<void()>,
                                                  absl::AnyInvocable<void()>,
                                                  absl::AnyInvocable<void()>) {
  ASSERT(0); // This constructor is not supported for OwnedImpl.
}

// NOLINTNEXTLINE(modernize-use-equals-default)
template <> MockBufferBase<Buffer::OwnedImpl>::MockBufferBase(){};

MockBufferFactory::MockBufferFactory() = default;
MockBufferFactory::~MockBufferFactory() = default;

} // namespace Envoy
