#include "source/common/stream_info/uint32_accessor_impl.h"

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

TEST(UInt32AccessorImplTest, TestProto) {
  uint32_t init_value = 0xdeadbeef;
  UInt32AccessorImpl accessor(init_value);
  auto message = accessor.serializeAsProto();
  EXPECT_NE(nullptr, message);

  auto* uint32_struct = dynamic_cast<ProtobufWkt::UInt32Value*>(message.get());
  EXPECT_NE(nullptr, uint32_struct);
  EXPECT_EQ(init_value, uint32_struct->value());
}

TEST(UInt32AccessorImplTest, TestString) {
  uint32_t init_value = 0xdeadbeef;
  UInt32AccessorImpl accessor(init_value);
  absl::optional<std::string> value = accessor.serializeAsString();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value, std::to_string(init_value));
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
