#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/utility.h"

#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
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

TEST(InputConstMemoryStream, All) {
  {
    InputConstMemoryStream istream{nullptr, 0};
    std::string s;
    istream >> s;
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(istream.eof());
  }

  {
    std::string data{"123"};
    InputConstMemoryStream istream{data.data(), data.size()};
    int x;
    istream >> x;
    EXPECT_EQ(123, x);
    EXPECT_TRUE(istream.eof());
  }
}

TEST(StringUtil, WhitespaceChars) {
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars, ' '));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars, '\t'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars, '\f'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars, '\v'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars, '\n'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars, '\r'));
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

TEST(StringUtil, StringViewLtrim) {
  EXPECT_EQ("", StringUtil::ltrim("     "));
  EXPECT_EQ("hello \t\f\v\n\r", StringUtil::ltrim("   hello \t\f\v\n\r"));
  EXPECT_EQ("hello ", StringUtil::ltrim("\t\f\v\n\r   hello "));
  EXPECT_EQ("a b ", StringUtil::ltrim("\t\f\v\n\ra b "));
  EXPECT_EQ("", StringUtil::ltrim("\t\f\v\n\r"));
  EXPECT_EQ("", StringUtil::ltrim(""));
}

TEST(StringUtil, StringViewRtrim) {
  EXPECT_EQ("", StringUtil::rtrim("     "));
  EXPECT_EQ("\t\f\v\n\rhello", StringUtil::rtrim("\t\f\v\n\rhello "));
  EXPECT_EQ("\t\f\v\n\r a b", StringUtil::rtrim("\t\f\v\n\r a b \t\f\v\n\r"));
  EXPECT_EQ("", StringUtil::rtrim("\t\f\v\n\r"));
  EXPECT_EQ("", StringUtil::rtrim(""));
}

TEST(StringUtil, StringViewTrim) {
  EXPECT_EQ("", StringUtil::trim("   "));
  EXPECT_EQ("hello", StringUtil::trim("\t\f\v\n\r  hello   "));
  EXPECT_EQ("he llo", StringUtil::trim(" \t\f\v\n\r he llo \t\f\v\n\r"));
}

TEST(StringUtil, StringViewCropRight) {
  EXPECT_EQ("hello", StringUtil::cropRight("hello; world\t\f\v\n\r", ";"));
  EXPECT_EQ("", StringUtil::cropRight(";hello world\t\f\v\n\r", ";"));
  EXPECT_EQ(" hel", StringUtil::cropRight(" hello alo\t\f\v\n\r", "lo"));
  EXPECT_EQ("\t\f\v\n\rhe 1", StringUtil::cropRight("\t\f\v\n\rhe 12\t\f\v\n\r", "2"));
  EXPECT_EQ("hello", StringUtil::cropRight("hello alo\t\f\v\n\r", " a"));
  EXPECT_EQ("hello ", StringUtil::cropRight("hello alo\t\f\v\n\r", "a", false));
  EXPECT_EQ("abcd", StringUtil::cropRight("abcd", ";"));
}

TEST(StringUtil, StringViewFindToken) {
  EXPECT_TRUE(StringUtil::findToken("hello; world", ";", "hello"));
  EXPECT_TRUE(StringUtil::findToken("abc; type=text", ";=", "text"));
  EXPECT_TRUE(StringUtil::findToken("abc; type=text", ";=", "abc"));
  EXPECT_TRUE(StringUtil::findToken("abc; type=text", ";=", "type"));
  EXPECT_FALSE(StringUtil::findToken("abc; type=text", ";=", " "));
  EXPECT_TRUE(StringUtil::findToken("abc; type=text", ";=", " type", false));
  EXPECT_FALSE(StringUtil::findToken("hello; world", ".", "hello"));
  EXPECT_TRUE(StringUtil::findToken("", ",", ""));
  EXPECT_FALSE(StringUtil::findToken("", "", "a"));
  EXPECT_TRUE(StringUtil::findToken(" ", " ", "", true));
  EXPECT_FALSE(StringUtil::findToken(" ", " ", "", false));
  EXPECT_TRUE(StringUtil::findToken("A=5", ".", "A=5"));
}

TEST(StringUtil, StringViewSplit) {
  {
    auto tokens = StringUtil::splitToken(" one , two , three ", ",", true);
    EXPECT_EQ(3, tokens.size());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " one ") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " two ") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " three ") != tokens.end());
    EXPECT_FALSE(std::find(tokens.begin(), tokens.end(), "one") != tokens.end());
  }
  {
    auto tokens = StringUtil::splitToken(" one , two , three ", ",");
    EXPECT_EQ(3, tokens.size());
    EXPECT_FALSE(std::find(tokens.begin(), tokens.end(), "one") != tokens.end());
    EXPECT_FALSE(std::find(tokens.begin(), tokens.end(), "two") != tokens.end());
    EXPECT_FALSE(std::find(tokens.begin(), tokens.end(), "three") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " one ") != tokens.end());
  }
  {
    auto tokens = StringUtil::splitToken(" one ,  , three=five ", ",=", true);
    EXPECT_EQ(4, tokens.size());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " one ") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "  ") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " three") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "five ") != tokens.end());
  }
  {
    EXPECT_EQ(std::vector<absl::string_view>{"hello"}, StringUtil::splitToken(",hello", ","));
    EXPECT_EQ(std::vector<absl::string_view>{}, StringUtil::splitToken("", ","));
    EXPECT_EQ(std::vector<absl::string_view>{"a"}, StringUtil::splitToken("a", ","));
    EXPECT_EQ(std::vector<absl::string_view>{"hello"}, StringUtil::splitToken("hello,", ","));
    EXPECT_EQ(std::vector<absl::string_view>{"hello"}, StringUtil::splitToken(",hello", ","));
    EXPECT_EQ(std::vector<absl::string_view>{"hello"}, StringUtil::splitToken("hello, ", ", "));
    EXPECT_EQ(std::vector<absl::string_view>{}, StringUtil::splitToken(",,", ","));

    EXPECT_THAT(std::vector<absl::string_view>({"h", "e", "l", "l", "o"}),
                ContainerEq(StringUtil::splitToken("hello", "")));
    EXPECT_THAT(std::vector<absl::string_view>({"hello", "world"}),
                ContainerEq(StringUtil::splitToken("hello world", " ")));
    EXPECT_THAT(std::vector<absl::string_view>({"hello", "world"}),
                ContainerEq(StringUtil::splitToken("hello   world", " ")));
    EXPECT_THAT(std::vector<absl::string_view>({"", "", "hello", "world"}),
                ContainerEq(StringUtil::splitToken("  hello world", " ", true)));
    EXPECT_THAT(std::vector<absl::string_view>({"hello", "world", ""}),
                ContainerEq(StringUtil::splitToken("hello world ", " ", true)));
    EXPECT_THAT(std::vector<absl::string_view>({"hello", "world"}),
                ContainerEq(StringUtil::splitToken("hello world", " ", true)));
  }
}

TEST(Primes, isPrime) {
  EXPECT_TRUE(Primes::isPrime(67));
  EXPECT_FALSE(Primes::isPrime(49));
  EXPECT_FALSE(Primes::isPrime(102));
  EXPECT_TRUE(Primes::isPrime(103));
}

TEST(Primes, findPrimeLargerThan) {
  EXPECT_EQ(67, Primes::findPrimeLargerThan(62));
  EXPECT_EQ(107, Primes::findPrimeLargerThan(103));
  EXPECT_EQ(10007, Primes::findPrimeLargerThan(9991));
}

TEST(RegexUtil, parseRegex) {
  EXPECT_THROW_WITH_REGEX(RegexUtil::parseRegex("(+invalid)"), EnvoyException,
                          "Invalid regex '\\(\\+invalid\\)': .+");

  {
    std::regex regex = RegexUtil::parseRegex("x*");
    EXPECT_NE(0, regex.flags() & std::regex::optimize);
  }

  {
    std::regex regex = RegexUtil::parseRegex("x*", std::regex::icase);
    EXPECT_NE(0, regex.flags() & std::regex::icase);
    EXPECT_EQ(0, regex.flags() & std::regex::optimize);
  }
}

} // namespace Envoy
