#include "source/common/stream_info/boolean_accessor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(BooleanAccessorImplTest, FalseValue) {
  BooleanAccessorImpl accessor(false);
  EXPECT_EQ(false, accessor.value());
}

TEST(BooleanAccessorImplTest, TrueValue) {
  BooleanAccessorImpl accessor(true);
  EXPECT_EQ(true, accessor.value());
}

TEST(BooleanAccessorImplTest, FalseValue) {
  BooleanAccessorImpl accessor(true);
  EXPECT_NE(nullptr, accessor.serializeAsProto());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
