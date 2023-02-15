#include "envoy/common/union_string.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(UnionStringTest, All) {
  // Static std::string constructor
  {
    std::string static_string("HELLO");
    UnionString string(static_string);
    EXPECT_EQ("HELLO", string.getStringView());
    EXPECT_EQ(static_string, string.getStringView());
    EXPECT_EQ(5U, string.size());
  }

  // Static move constructor
  {
    std::string static_string("HELLO");
    UnionString string1(static_string);
    UnionString string2(std::move(string1));
    EXPECT_EQ("HELLO", string2.getStringView());
    EXPECT_EQ(static_string, string1.getStringView()); // NOLINT
    EXPECT_EQ(static_string, string2.getStringView());
    EXPECT_EQ(5U, string1.size());
    EXPECT_EQ(5U, string2.size());
  }

  // Inline move constructor
  {
    UnionString string;
    string.setCopy("hello");
    EXPECT_FALSE(string.isReference());
    UnionString string2(std::move(string));
    EXPECT_TRUE(string.empty());        // NOLINT
    EXPECT_FALSE(string.isReference()); // NOLINT
    EXPECT_FALSE(string2.isReference());
    string.append("world", 5);
    EXPECT_EQ("world", string.getStringView());
    EXPECT_EQ(5UL, string.size());
    EXPECT_EQ("hello", string2.getStringView());
    EXPECT_EQ(5UL, string2.size());
  }

  // Inline move large constructor
  {
    std::string large(4096, 'a');
    UnionString string;
    string.setCopy(large);
    EXPECT_FALSE(string.isReference());
    UnionString string2(std::move(string));
    EXPECT_TRUE(string.empty());        // NOLINT
    EXPECT_FALSE(string.isReference()); // NOLINT
    EXPECT_FALSE(string2.isReference());
    string.append("b", 1);
    EXPECT_EQ("b", string.getStringView());
    EXPECT_EQ(1UL, string.size());
    EXPECT_EQ(large, string2.getStringView());
    EXPECT_EQ(4096UL, string2.size());
  }

  // Static to inline number.
  {
    std::string static_string("HELLO");
    UnionString string(static_string);
    string.setInteger(5);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ("5", string.getStringView());
  }

  // Static to inline string.
  {
    std::string static_string("HELLO");
    UnionString string(static_string);
    string.setCopy(static_string);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ("HELLO", string.getStringView());
  }

  // Inline rtrim removes trailing whitespace only.
  {
    const std::string data_with_leading_lws = " \t\f\v  data";
    const std::string data_with_leading_and_trailing_lws = data_with_leading_lws + " \t\f\v";
    UnionString string;
    string.append(data_with_leading_and_trailing_lws.data(),
                  data_with_leading_and_trailing_lws.size());
    EXPECT_EQ(data_with_leading_and_trailing_lws, string.getStringView());
    string.rtrim();
    EXPECT_NE(data_with_leading_and_trailing_lws, string.getStringView());
    EXPECT_EQ(data_with_leading_lws, string.getStringView());
  }

  // Static clear() does nothing.
  {
    std::string static_string("HELLO");
    UnionString string(static_string);
    EXPECT_TRUE(string.isReference());
    string.clear();
    EXPECT_TRUE(string.isReference());
    EXPECT_EQ("HELLO", string.getStringView());
  }

  // Static to append.
  {
    std::string static_string("HELLO");
    UnionString string(static_string);
    EXPECT_TRUE(string.isReference());
    string.append("a", 1);
    EXPECT_EQ("HELLOa", string.getStringView());
  }

  // Copy inline
  {
    UnionString string;
    string.setCopy("hello");
    EXPECT_EQ("hello", string.getStringView());
    EXPECT_EQ(5U, string.size());
  }

  // Copy dynamic
  {
    UnionString string;
    std::string large_value(4096, 'a');
    string.setCopy(large_value);
    EXPECT_EQ(large_value, string.getStringView());
    EXPECT_NE(large_value.c_str(), string.getStringView().data());
    EXPECT_EQ(4096U, string.size());
  }

  // Copy twice dynamic
  {
    UnionString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1);
    std::string large_value2(2048, 'b');
    string.setCopy(large_value2);
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(2048U, string.size());
  }

  // Copy twice dynamic with reallocate
  {
    UnionString string;
    std::string large_value1(4096, 'a');
    string.setCopy(large_value1);
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2);
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(16384U, string.size());
  }

  // Copy twice inline to dynamic
  {
    UnionString string;
    std::string large_value1(16, 'a');
    string.setCopy(large_value1);
    std::string large_value2(16384, 'b');
    string.setCopy(large_value2);
    EXPECT_EQ(large_value2, string.getStringView());
    EXPECT_NE(large_value2.c_str(), string.getStringView().data());
    EXPECT_EQ(16384U, string.size());
  }

  // Copy, exactly filling inline capacity
  //
  // ASAN does not catch the clobber in the case where the code writes one past the
  // end of the inline buffer. To ensure coverage the next block checks that setCopy
  // is not introducing a NUL in a way that does not rely on an actual clobber getting
  // detected.
  {
    UnionString string;
    std::string large(128, 'z');
    string.setCopy(large);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(string.getStringView(), large);
  }

  // Copy, exactly filling dynamic capacity
  //
  // ASAN should catch a write one past the end of the inline buffer. This test
  // forces a dynamic buffer with one copy and then fills it with the next.
  {
    UnionString string;
    // Force dynamic vector allocation with setCopy of inline buffer size + 1.
    std::string large1(129, 'z');
    string.setCopy(large1);
    EXPECT_FALSE(string.isReference());
    // Dynamic capacity in setCopy is 2x required by the size.
    // So to fill it exactly setCopy with a total of 256 chars.
    std::string large2(256, 'z');
    string.setCopy(large2);
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(string.getStringView(), large2);
  }

  // Append, small buffer to inline
  {
    UnionString string;
    std::string test(128, 'a');
    string.append(test.c_str(), test.size());
    EXPECT_FALSE(string.isReference());
    string.append("a", 1);
    EXPECT_FALSE(string.isReference());
    test += 'a';
    EXPECT_EQ(test, string.getStringView());
  }

  // Append into inline twice, then shift to dynamic.
  {
    UnionString string;
    string.append("hello", 5);
    EXPECT_EQ("hello", string.getStringView());
    EXPECT_EQ(5U, string.size());
    string.append("world", 5);
    EXPECT_EQ("helloworld", string.getStringView());
    EXPECT_EQ(10U, string.size());
    std::string large(4096, 'a');
    string.append(large.c_str(), large.size());
    large = "helloworld" + large;
    EXPECT_EQ(large, string.getStringView());
    EXPECT_EQ(4106U, string.size());
  }

  // Append, realloc close to limit with small buffer.
  {
    UnionString string;
    std::string large(129, 'a');
    string.append(large.c_str(), large.size());
    EXPECT_FALSE(string.isReference());
    std::string large2(120, 'b');
    string.append(large2.c_str(), large2.size());
    std::string large3(32, 'c');
    string.append(large3.c_str(), large3.size());
    EXPECT_EQ((large + large2 + large3), string.getStringView());
    EXPECT_EQ(281U, string.size());
  }

  // Append, exactly filling dynamic capacity
  //
  // ASAN should catch a write one past the end of the dynamic buffer. This test
  // forces a dynamic buffer with one copy and then fills it with the next.
  {
    UnionString string;
    // Force dynamic allocation with setCopy of inline buffer size + 1.
    std::string large1(129, 'z');
    string.setCopy(large1);
    EXPECT_FALSE(string.isReference());
    // Dynamic capacity in setCopy is 2x required by the size.
    // So to fill it exactly append 127 chars for a total of 256 chars.
    std::string large2(127, 'z');
    string.append(large2.c_str(), large2.size());
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(string.getStringView(), large1 + large2);
  }

  // Set integer, inline
  {
    UnionString string;
    string.setInteger(123456789);
    EXPECT_EQ("123456789", string.getStringView());
    EXPECT_EQ(9U, string.size());
  }

  // Set integer, dynamic
  {
    UnionString string;
    std::string large(129, 'a');
    string.append(large.c_str(), large.size());
    string.setInteger(123456789);
    EXPECT_EQ("123456789", string.getStringView());
    EXPECT_EQ(9U, string.size());
    EXPECT_FALSE(string.isReference());
  }

  // Set static, switch to inline, back to static.
  {
    const std::string static_string = "hello world";
    UnionString string;
    string.setReference(static_string);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(11U, string.size());
    EXPECT_TRUE(string.isReference());

    const std::string large(129, 'a');
    string.setCopy(large);
    EXPECT_NE(string.getStringView().data(), large.c_str());
    EXPECT_FALSE(string.isReference());

    string.setReference(static_string);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(11U, string.size());
    EXPECT_TRUE(string.isReference());
  }

  // getString
  {
    std::string static_string("HELLO");
    UnionString UnionString1(static_string);
    absl::string_view retString1 = UnionString1.getStringView();
    EXPECT_EQ("HELLO", retString1);
    EXPECT_EQ(5U, retString1.size());

    UnionString UnionString2;
    absl::string_view retString2 = UnionString2.getStringView();
    EXPECT_EQ(0U, retString2.size());
  }

  // inlineTransform
  {
    const std::string static_string = "HELLO";
    UnionString string;
    string.setCopy(static_string);
    string.inlineTransform([](char c) { return static_cast<uint8_t>(tolower(c)); });
    EXPECT_FALSE(string.isReference());
    EXPECT_EQ(5U, string.size());
    EXPECT_EQ(string.getStringView(), "hello");
    string.inlineTransform(toupper);
    EXPECT_EQ(string.getStringView(), static_string);
    EXPECT_EQ(5U, string.size());
    EXPECT_FALSE(string.isReference());
  }
}

} // namespace
} // namespace Envoy
