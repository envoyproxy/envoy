#include "common/buffer/buffer_impl.h"

#include "gtest/gtest.h"
#include "library/common/buffer/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Buffer {

TEST(DataConstructorTest, FromCToCppEmpty) {
  envoy_data empty_data = {0, nullptr, free, nullptr};

  InstancePtr cpp_data = Utility::toInternalData(empty_data);

  ASSERT_EQ(cpp_data->length(), 0);
}

TEST(DataConstructorTest, FromCToCpp) {
  std::string s = "test string";
  envoy_data c_data = {s.size(), reinterpret_cast<const uint8_t*>(s.c_str()), free, nullptr};
  ;

  InstancePtr cpp_data = Utility::toInternalData(c_data);

  ASSERT_EQ(cpp_data->length(), c_data.length);
  ASSERT_EQ(cpp_data->toString(), s);
}

TEST(DataConstructorTest, FromCppToCEmpty) {
  OwnedImpl empty_data;

  envoy_data c_data = Utility::toBridgeData(empty_data);

  ASSERT_EQ(c_data.length, 0);
  c_data.release(c_data.context);
}

TEST(DataConstructorTest, FromCppToC) {
  std::string s = "test string";
  OwnedImpl cpp_data = OwnedImpl(absl::string_view(s));

  envoy_data c_data = Utility::toBridgeData(cpp_data);

  ASSERT_EQ(c_data.length, s.size());
  ASSERT_EQ(Http::Utility::convertToString(c_data), s);
  c_data.release(c_data.context);
}

TEST(DataConstructorTest, CopyFromCppToC) {
  std::string s = "test string";
  OwnedImpl cpp_data = OwnedImpl(absl::string_view(s));

  envoy_data c_data = Utility::copyToBridgeData(cpp_data);

  ASSERT_EQ(c_data.length, s.size());
  ASSERT_EQ(Http::Utility::convertToString(c_data), s);
  c_data.release(c_data.context);
}

TEST(DataConstructorTest, CopyStringFromCppToC) {
  std::string s = "test string";

  envoy_data c_data = Utility::copyToBridgeData(s);

  ASSERT_EQ(c_data.length, s.size());
  ASSERT_EQ(Http::Utility::convertToString(c_data), s);
  c_data.release(c_data.context);
}

} // namespace Buffer
} // namespace Envoy
