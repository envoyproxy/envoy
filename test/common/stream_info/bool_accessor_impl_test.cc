#include "source/common/stream_info/bool_accessor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(BoolAccessorImplTest, FalseValue) {
  BoolAccessorImpl accessor(false);
  EXPECT_EQ(false, accessor.value());
}

TEST(BoolAccessorImplTest, TrueValue) {
  BoolAccessorImpl accessor(true);
  EXPECT_EQ(true, accessor.value());
}

TEST(BoolAccessorImplTest, TestProto) {
  BoolAccessorImpl accessor(true);
  auto message = accessor.serializeAsProto();
  EXPECT_NE(nullptr, message);
}

TEST(BoolAccessorImplTest, TestString) {
  BoolAccessorImpl accessor(true);
  auto str = accessor.serializeAsString();
  EXPECT_EQ("true", str);
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
