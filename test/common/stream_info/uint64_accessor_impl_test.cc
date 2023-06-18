#include "source/common/stream_info/uint64_accessor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(UInt64AccessorImplTest, ConstructorInitsValue) {
  uint64_t init_value = 0xdeadbeefdeadbeef;
  UInt64AccessorImpl accessor(init_value);
  EXPECT_EQ(init_value, accessor.value());
}

TEST(UInt64AccessorImplTest, IncrementValue) {
  uint64_t init_value = 0xdeadbeefdeadbeef;
  UInt64AccessorImpl accessor(init_value);
  accessor.increment();
            
  EXPECT_EQ(0xdeadbeefdeadbef0, accessor.value());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
