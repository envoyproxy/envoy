#include "source/common/buffer/buffer_impl.h"

#include "gtest/gtest.h"
#include "library/common/bridge/utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Bridge {

TEST(EnvoyMapConvenientInitializerTest, FromCppToCEmpty) {
  const auto map = Utility::makeEnvoyMap({});

  EXPECT_EQ(map.length, 0);
  release_envoy_map(map);
}

TEST(EnvoyMapConvenientInitializerTest, FromCppToC) {
  const auto map = Utility::makeEnvoyMap({{"foo", "bar"}});

  EXPECT_EQ(Bridge::Utility::copyToString(map.entries[0].key), "foo");
  EXPECT_EQ(Bridge::Utility::copyToString(map.entries[0].value), "bar");
  EXPECT_EQ(map.length, 1);
  release_envoy_map(map);
}

TEST(DataConstructorTest, FromCToCppEmpty) {
  envoy_data empty_data = {0, nullptr, free, nullptr};

  Buffer::InstancePtr cpp_data = Utility::toInternalData(empty_data);

  ASSERT_EQ(cpp_data->length(), 0);
}

TEST(DataConstructorTest, FromCToCpp) {
  std::string s = "test string";
  envoy_data c_data = {s.size(), reinterpret_cast<const uint8_t*>(s.c_str()), free, nullptr};
  ;

  Buffer::InstancePtr cpp_data = Utility::toInternalData(c_data);

  ASSERT_EQ(cpp_data->length(), c_data.length);
  ASSERT_EQ(cpp_data->toString(), s);
}

TEST(DataConstructorTest, FromCppToCEmpty) {
  Buffer::OwnedImpl empty_data;

  envoy_data c_data = Utility::toBridgeData(empty_data);

  ASSERT_EQ(c_data.length, 0);
  release_envoy_data(c_data);
}

TEST(DataConstructorTest, FromCppToC) {
  std::string s = "test string";
  Buffer::OwnedImpl cpp_data = Buffer::OwnedImpl(absl::string_view(s));

  envoy_data c_data = Utility::toBridgeData(cpp_data);

  ASSERT_EQ(c_data.length, s.size());
  ASSERT_EQ(Utility::copyToString(c_data), s);
  release_envoy_data(c_data);
}

TEST(DataConstructorTest, FromCppToCPartial) {
  std::string s = "test string";
  Buffer::OwnedImpl cpp_data = Buffer::OwnedImpl(absl::string_view(s));

  envoy_data c_data = Utility::toBridgeData(cpp_data, 4);

  ASSERT_EQ(c_data.length, 4);
  ASSERT_EQ(Utility::copyToString(c_data), "test");
  ASSERT_EQ(cpp_data.length(), 7);
  ASSERT_EQ(cpp_data.toString(), " string");
  release_envoy_data(c_data);
}

TEST(DataConstructorTest, CopyFromCppToC) {
  std::string s = "test string";
  Buffer::OwnedImpl cpp_data = Buffer::OwnedImpl(absl::string_view(s));

  envoy_data c_data = Utility::copyToBridgeData(cpp_data);

  ASSERT_EQ(c_data.length, s.size());
  ASSERT_EQ(Utility::copyToString(c_data), s);
  release_envoy_data(c_data);
}

TEST(DataConstructorTest, CopyFromCppToCPartial) {
  std::string s = "test string";
  Buffer::OwnedImpl cpp_data = Buffer::OwnedImpl(absl::string_view(s));

  envoy_data c_data = Utility::copyToBridgeData(cpp_data, 4);

  ASSERT_EQ(c_data.length, 4);
  ASSERT_EQ(Utility::copyToString(c_data), "test");
  ASSERT_EQ(cpp_data.length(), 11);
  ASSERT_EQ(cpp_data.toString(), "test string");
  release_envoy_data(c_data);
}

TEST(DataConstructorTest, CopyStringFromCppToC) {
  std::string s = "test string";

  envoy_data c_data = Utility::copyToBridgeData(s);

  ASSERT_EQ(c_data.length, s.size());
  ASSERT_EQ(Utility::copyToString(c_data), s);
  release_envoy_data(c_data);
}

} // namespace Bridge
} // namespace Envoy
