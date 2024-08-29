#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/packed_struct.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

class PackedStructTest : public testing::Test {
public:
  template <class T, uint8_t max_size, class ElementName>
  size_t capacity(const PackedStruct<T, max_size, ElementName>& packed_struct) const {
    return packed_struct.capacity();
  }
  template <class T, uint8_t max_size, class ElementName, ElementName element_name>
  const T& testConstAccess(const PackedStruct<T, max_size, ElementName>& packed_struct) const {
    return packed_struct.template get<element_name>().ref();
  }
};

namespace {

TEST_F(PackedStructTest, StringStruct) {
  enum class RedirectStringElement { SchemeRedirect, HostRedirect, PathRedirect };
  using RedirectStringsPackedStruct = PackedStruct<std::string, 3, RedirectStringElement>;

  Memory::TestUtil::MemoryTest memory_test;
  // Initialize capacity to 2.
  RedirectStringsPackedStruct redirect_strings(2);
  redirect_strings.set<RedirectStringElement::SchemeRedirect>("abc");
  redirect_strings.set<RedirectStringElement::PathRedirect>("def");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings.size(), 2);
  EXPECT_EQ(capacity(redirect_strings), 2);
  EXPECT_EQ(redirect_strings.get<RedirectStringElement::SchemeRedirect>().ref(), "abc");
  EXPECT_EQ(redirect_strings.get<RedirectStringElement::PathRedirect>().ref(), "def");
  EXPECT_FALSE(redirect_strings.get<RedirectStringElement::HostRedirect>().has_value());

  // Add a third element.
  redirect_strings.set<RedirectStringElement::HostRedirect>("abcd");
  EXPECT_EQ(redirect_strings.get<RedirectStringElement::HostRedirect>().ref(), "abcd");
  auto& const_val =
      testConstAccess<std::string, 3, RedirectStringElement, RedirectStringElement::HostRedirect>(
          redirect_strings);
  EXPECT_EQ(const_val, "abcd");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 3 * sizeof(std::string) + 16);
  EXPECT_EQ(redirect_strings.size(), 3);
  EXPECT_EQ(capacity(redirect_strings), 3);
}

// Test the move constructor and move assignment operators. Verify that no
// memory is allocated on the heap because of these operations.
TEST_F(PackedStructTest, StringStructMove) {
  enum class RedirectStringElement { SchemeRedirect, HostRedirect, PathRedirect };
  using RedirectStringsPackedStruct = PackedStruct<std::string, 3, RedirectStringElement>;

  Memory::TestUtil::MemoryTest memory_test;
  // Initialize capacity to 2.
  RedirectStringsPackedStruct redirect_strings(2);
  redirect_strings.set<RedirectStringElement::SchemeRedirect>("abc");
  redirect_strings.set<RedirectStringElement::PathRedirect>("def");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings.size(), 2);
  EXPECT_EQ(capacity(redirect_strings), 2);
  EXPECT_EQ(redirect_strings.get<RedirectStringElement::SchemeRedirect>().ref(), "abc");
  EXPECT_EQ(redirect_strings.get<RedirectStringElement::PathRedirect>().ref(), "def");
  EXPECT_FALSE(redirect_strings.get<RedirectStringElement::HostRedirect>().has_value());

  // Invoke move constructor.
  RedirectStringsPackedStruct redirect_strings2(std::move(redirect_strings));
  // Ensure no memory was allocated on the heap by the move constructor.
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings2.size(), 2);
  EXPECT_EQ(capacity(redirect_strings2), 2);
  EXPECT_EQ(redirect_strings2.get<RedirectStringElement::SchemeRedirect>().ref(), "abc");
  EXPECT_EQ(redirect_strings2.get<RedirectStringElement::PathRedirect>().ref(), "def");
  EXPECT_FALSE(redirect_strings2.get<RedirectStringElement::HostRedirect>().has_value());

  // Invoke move assignment.
  RedirectStringsPackedStruct redirect_strings3(0);
  redirect_strings3 = std::move(redirect_strings2);
  // Ensure no memory was allocated on the heap by the move assignment.
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings3.size(), 2);
  EXPECT_EQ(capacity(redirect_strings3), 2);
  EXPECT_EQ(redirect_strings3.get<RedirectStringElement::SchemeRedirect>().ref(), "abc");
  EXPECT_EQ(redirect_strings3.get<RedirectStringElement::PathRedirect>().ref(), "def");
  EXPECT_FALSE(redirect_strings3.get<RedirectStringElement::HostRedirect>().has_value());
}

} // namespace
} // namespace Envoy
