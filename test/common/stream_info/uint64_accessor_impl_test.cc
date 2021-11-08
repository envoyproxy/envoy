#include "source/common/stream_info/uint64_accessor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(UInt64AccessorImplTest, ConstructorInitsValue) {
  uint64_t init_value = 0xdeadbeef;
  UInt64AccessorImpl accessor(init_value);
  EXPECT_EQ(init_value, accessor.value());
}

TEST(UInt64AccessorImplTest, DebugString) {
  uint64_t init_value = 123;
  UInt64AccessorImpl accessor(init_value);
  EXPECT_EQ("value: 123\n", accessor.serializeAsProto()->DebugString());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
