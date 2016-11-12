#include "common/common/utility.h"

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

TEST(StringUtil, caseInsensitiveCompare) {
  EXPECT_EQ(0, StringUtil::caseInsensitiveCompare("CONTENT-LENGTH", "content-length"));
  EXPECT_LT(0, StringUtil::caseInsensitiveCompare("CONTENT-LENGTH", "blah"));
  EXPECT_GT(0, StringUtil::caseInsensitiveCompare("CONTENT-LENGTH", "hello"));
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
    size_t result;
    result = StringUtil::strlcpy(dest, std::string{"hello"}.c_str(), sizeof(dest));
    EXPECT_STREQ("hello", dest);
    EXPECT_EQ(5U, result);
  }

  {
    char dest[6];
    size_t result;
    result = StringUtil::strlcpy(dest, std::string{"hello"}.c_str(), 3);
    EXPECT_STREQ("he", dest);
    EXPECT_EQ(5U, result);
  }

  {
    char dest[3];
    size_t result;
    result = StringUtil::strlcpy(dest, std::string{"hello"}.c_str(), sizeof(dest));
    EXPECT_STREQ("he", dest);
    EXPECT_EQ(5U, result);
  }

  {
    char dest[3];
    size_t result;
    result = StringUtil::strlcpy(dest, std::string{""}.c_str(), sizeof(dest));
    EXPECT_STREQ("", dest);
    EXPECT_EQ(0U, result);
  }
}

TEST(StringUtil, split) {
  EXPECT_EQ(std::vector<std::string>{}, StringUtil::split("", ','));
  EXPECT_EQ(std::vector<std::string>{"a"}, StringUtil::split("a", ','));
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split(",hello", ','));
  EXPECT_EQ(std::vector<std::string>{"hello"}, StringUtil::split("hello,", ','));
  EXPECT_EQ(std::vector<std::string>{}, StringUtil::split(",,", ','));
  EXPECT_EQ((std::vector<std::string>{"hello", "world"}), StringUtil::split("hello,world", ','));
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
