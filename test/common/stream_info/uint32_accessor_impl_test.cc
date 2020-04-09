#include "common/stream_info/uint32_accessor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(UInt32AccessorImplTest, ConstructorInitsValue) {
  uint32_t init_value = 0xdeadbeef;
  UInt32AccessorImpl accessor(init_value);
  EXPECT_EQ(init_value, accessor.value());
}

TEST(UInt32AccessorImplTest, IncrementValue) {
  uint32_t init_value = 0xdeadbeef;
  UInt32AccessorImpl accessor(init_value);
  accessor.increment();
  EXPECT_EQ(0xdeadbef0, accessor.value());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
