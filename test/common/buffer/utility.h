#pragma once

#include <initializer_list>

#include "common/buffer/buffer_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

/** Used to specify which OwnedImpl implementation to test. */
enum class BufferImplementation {
  Old, // original evbuffer-based version
  New  // new deque-of-slices version
};

/**
 * Base class for tests that are parameterized based on BufferImplementation.
 */
class BufferImplementationParamTest : public testing::TestWithParam<BufferImplementation> {
protected:
  BufferImplementationParamTest() {
    OwnedImpl::useOldImpl(GetParam() == BufferImplementation::Old);
  }

  ~BufferImplementationParamTest() override = default;

  /** Verify that a buffer has been constructed using the expected implementation. */
  void verifyImplementation(const OwnedImpl& buffer) {
    switch (GetParam()) {
    case BufferImplementation::Old:
      ASSERT_TRUE(buffer.usesOldImpl());
      break;
    case BufferImplementation::New:
      ASSERT_FALSE(buffer.usesOldImpl());
      break;
    }
  }
};

inline void addRepeated(Buffer::Instance& buffer, int n, int8_t value) {
  for (int i = 0; i < n; i++) {
    buffer.add(&value, 1);
  }
}

inline void addSeq(Buffer::Instance& buffer, const std::initializer_list<uint8_t> values) {
  for (int8_t value : values) {
    buffer.add(&value, 1);
  }
}

} // namespace
} // namespace Buffer
} // namespace Envoy
