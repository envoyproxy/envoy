#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;

namespace Envoy {

TEST(StringUtil, atoul) {
  uint64_t out;
  EXPECT_FALSE(StringUtil::atoul("123b", out));
  EXPECT_FALSE(StringUtil::atoul("", out));
  EXPECT_FALSE(StringUtil::atoul("b123", out));

  EXPECT_TRUE(StringUtil::atoul("123", out));
  EXPECT_EQ(123U, out);

  EXPECT_TRUE(StringUtil::atoul("  456", out));
  EXPECT_EQ(456U, out);

  EXPECT_TRUE(StringUtil::atoul("00789", out));
  EXPECT_EQ(789U, out);
}

TEST(DateUtil, All) {
  EXPECT_FALSE(DateUtil::timePointValid(SystemTime()));
  EXPECT_TRUE(DateUtil::timePointValid(std::chrono::system_clock::now()));
}

TEST(ProdSystemTimeSourceTest, All) {
  ProdSystemTimeSource source;
  source.currentTime();
}

TEST(StringUtil, caseInsensitiveCompare) {
  EXPECT_EQ(0, StringUtil::caseInsensitiveCompare("CONTENT-LENGTH", "content-length"));
  EXPECT_LT(0, StringUtil::caseInsensitiveCompare("CONTENT-LENGTH", "blah"));
  EXPECT_GT(0, StringUtil::caseInsensitiveCompare("CONTENT-LENGTH", "hello"));
}

TEST(StringUtil, itoa) {
  char buf[32];
  EXPECT_THROW(StringUtil::itoa(buf, 20, 1), std::invalid_argument);

  EXPECT_EQ(1UL, StringUtil::itoa(buf, sizeof(buf), 0));
  EXPECT_STREQ("0", buf);

  EXPECT_EQ(2UL, StringUtil::itoa(buf, sizeof(buf), 10));
  EXPECT_STREQ("10", buf);

  EXPECT_EQ(10UL, StringUtil::itoa(buf, sizeof(buf), 1234567890));
  EXPECT_STREQ("1234567890", buf);

  EXPECT_EQ(20UL, StringUtil::itoa(buf, sizeof(buf), std::numeric_limits<uint64_t>::max()));
  EXPECT_STREQ("18446744073709551615", buf);
}

TEST(StringUtil, rtrim) {
  {
    std::string test("   ");
    StringUtil::rtrim(test);
    EXPECT_EQ("", test);
  }

  {
    std::string test("   hello \r\t\r\n");
    StringUtil::rtrim(test);
    EXPECT_EQ("   hello", test);
  }
}

TEST(StringUtil, strlcpy) {
  {
    char dest[6];
    EXPECT_EQ(5U, StringUtil::strlcpy(dest, std::string{"hello"}.c_str(), sizeof(dest)));
    EXPECT_STREQ("hello", dest);
  }

  {
    char dest[6];
    EXPECT_EQ(5U, StringUtil::strlcpy(dest, std::string{"hello"}.c_str(), 3));
    EXPECT_STREQ("he", dest);
  }

  {
    char dest[3];
    EXPECT_EQ(5U, StringUtil::strlcpy(dest, std::string{"hello"}.c_str(), sizeof(dest)));
    EXPECT_STREQ("he", dest);
  }

  {
    char dest[3];
    EXPECT_EQ(0U, StringUtil::strlcpy(dest, std::string{""}.c_str(), sizeof(dest)));
    EXPECT_STREQ("", dest);
  }

  {
    char dest[3] = "yo";

    EXPECT_EQ(1U, StringUtil::strlcpy(dest, std::string{"a"}.c_str(), sizeof(dest)));
    EXPECT_STREQ("a", dest);

    EXPECT_EQ(10U, StringUtil::strlcpy(dest, std::string{"absolutely"}.c_str(), sizeof(dest)));
    EXPECT_STREQ("ab", dest);
  }
}

TEST(StringUtil, split) {
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split(",hello", ','));
  EXPECT_EQ(std::vector<std::string>{}, StringUtil::split("", ","));
  EXPECT_EQ(std::vector<std::string>{"a"}, StringUtil::split("a", ","));
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split("hello,", ","));
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split(",hello", ","));
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split("hello, ", ", "));
  EXPECT_EQ(std::vector<std::string>{}, StringUtil::split(",,", ","));
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split("hello", ""));

  EXPECT_THAT(std::vector<std::string>({"hello", "world"}),
              ContainerEq(StringUtil::split("hello world", " ")));
  EXPECT_THAT(std::vector<std::string>({"hello", "world"}),
              ContainerEq(StringUtil::split("hello   world", " ")));

  EXPECT_THAT(std::vector<std::string>({"", "", "hello", "world"}),
              ContainerEq(StringUtil::split("  hello world", " ", true)));
  EXPECT_THAT(std::vector<std::string>({"hello", "world", ""}),
              ContainerEq(StringUtil::split("hello world ", " ", true)));
  EXPECT_THAT(std::vector<std::string>({"hello", "world"}),
              ContainerEq(StringUtil::split("hello world", " ", true)));
  EXPECT_THAT(std::vector<std::string>({"hello", "", "", "world"}),
              ContainerEq(StringUtil::split("hello   world", " ", true)));
}

TEST(StringUtil, join) {
  EXPECT_EQ("hello,world", StringUtil::join({"hello", "world"}, ","));
  EXPECT_EQ("hello", StringUtil::join({"hello"}, ","));
  EXPECT_EQ("", StringUtil::join({}, ","));

  EXPECT_EQ("helloworld", StringUtil::join({"hello", "world"}, ""));
  EXPECT_EQ("hello", StringUtil::join({"hello"}, ""));
  EXPECT_EQ("", StringUtil::join({}, ""));

  EXPECT_EQ("hello,,world", StringUtil::join({"hello", "world"}, ",,"));
  EXPECT_EQ("hello", StringUtil::join({"hello"}, ",,"));
  EXPECT_EQ("", StringUtil::join({}, ",,"));
}

TEST(StringUtil, endsWith) {
  EXPECT_TRUE(StringUtil::endsWith("test", "st"));
  EXPECT_TRUE(StringUtil::endsWith("t", "t"));
  EXPECT_TRUE(StringUtil::endsWith("test", ""));
  EXPECT_TRUE(StringUtil::endsWith("", ""));
  EXPECT_FALSE(StringUtil::endsWith("test", "ttest"));
  EXPECT_FALSE(StringUtil::endsWith("test", "w"));
}

TEST(StringUtil, startsWith) {
  EXPECT_TRUE(StringUtil::startsWith("Test", "Te"));
  EXPECT_TRUE(StringUtil::startsWith("Test", "Te", false));
  EXPECT_TRUE(StringUtil::startsWith("Test", "te", false));
  EXPECT_TRUE(StringUtil::startsWith("", ""));
  EXPECT_TRUE(StringUtil::startsWith("test", ""));
  EXPECT_FALSE(StringUtil::startsWith("Test", "te"));
  EXPECT_FALSE(StringUtil::startsWith("Test", "tE", true));
  EXPECT_FALSE(StringUtil::startsWith("test", "boo", true));
  EXPECT_FALSE(StringUtil::startsWith("test", "boo", false));
  EXPECT_FALSE(StringUtil::startsWith("test", "testtest"));
  EXPECT_FALSE(StringUtil::startsWith("test", "TESTTEST", false));
  EXPECT_FALSE(StringUtil::startsWith("", "test"));
}

TEST(StringUtil, escape) {
  EXPECT_EQ(StringUtil::escape("hello world"), "hello world");
  EXPECT_EQ(StringUtil::escape("hello\nworld\n"), "hello\\nworld\\n");
  EXPECT_EQ(StringUtil::escape("\t\nworld\r\n"), "\\t\\nworld\\r\\n");
  EXPECT_EQ(StringUtil::escape("{\"linux\": \"penguin\"}"), "{\\\"linux\\\": \\\"penguin\\\"}");
}

TEST(StringUtil, toUpper) {
  EXPECT_EQ(StringUtil::toUpper(""), "");
  EXPECT_EQ(StringUtil::toUpper("a"), "A");
  EXPECT_EQ(StringUtil::toUpper("Ba"), "BA");
  EXPECT_EQ(StringUtil::toUpper("X asdf aAf"), "X ASDF AAF");
}

} // namespace Envoy
