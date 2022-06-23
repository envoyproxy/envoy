#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"

#include "source/common/common/utility.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::ElementsAre;
using testing::WhenSorted;
#ifdef WIN32
using testing::HasSubstr;
using testing::Not;
#endif

namespace Envoy {

TEST(IntUtil, roundUpToMultiple) {
  // Round up to non-power-of-2
  EXPECT_EQ(3, IntUtil::roundUpToMultiple(1, 3));
  EXPECT_EQ(3, IntUtil::roundUpToMultiple(3, 3));
  EXPECT_EQ(6, IntUtil::roundUpToMultiple(4, 3));
  EXPECT_EQ(6, IntUtil::roundUpToMultiple(5, 3));
  EXPECT_EQ(6, IntUtil::roundUpToMultiple(6, 3));
  EXPECT_EQ(21, IntUtil::roundUpToMultiple(20, 3));
  EXPECT_EQ(21, IntUtil::roundUpToMultiple(21, 3));

  // Round up to power-of-2
  EXPECT_EQ(0, IntUtil::roundUpToMultiple(0, 4));
  EXPECT_EQ(4, IntUtil::roundUpToMultiple(3, 4));
  EXPECT_EQ(4, IntUtil::roundUpToMultiple(4, 4));
  EXPECT_EQ(8, IntUtil::roundUpToMultiple(5, 4));
  EXPECT_EQ(8, IntUtil::roundUpToMultiple(8, 4));
  EXPECT_EQ(24, IntUtil::roundUpToMultiple(21, 4));
  EXPECT_EQ(24, IntUtil::roundUpToMultiple(24, 4));
}

TEST(StringUtil, strtoull) {
  uint64_t out;
  const char* rest;

  static const char* test_str = "12345b";
  rest = StringUtil::strtoull(test_str, out);
  EXPECT_NE(nullptr, rest);
  EXPECT_EQ('b', *rest);
  EXPECT_EQ(&test_str[5], rest);
  EXPECT_EQ(12345U, out);

  EXPECT_EQ(nullptr, StringUtil::strtoull("", out));
  EXPECT_EQ(nullptr, StringUtil::strtoull("b123", out));

  rest = StringUtil::strtoull("123", out);
  EXPECT_NE(nullptr, rest);
  EXPECT_EQ('\0', *rest);
  EXPECT_EQ(123U, out);

  EXPECT_NE(nullptr, StringUtil::strtoull("  456", out));
  EXPECT_EQ(456U, out);

  EXPECT_NE(nullptr, StringUtil::strtoull("00789", out));
  EXPECT_EQ(789U, out);

  // Hex
  rest = StringUtil::strtoull("0x1234567890abcdefg", out, 16);
  EXPECT_NE(nullptr, rest);
  EXPECT_EQ('g', *rest);
  EXPECT_EQ(0x1234567890abcdefU, out);

  // Explicit decimal
  rest = StringUtil::strtoull("01234567890A", out, 10);
  EXPECT_NE(nullptr, rest);
  EXPECT_EQ('A', *rest);
  EXPECT_EQ(1234567890U, out);

  // Octal
  rest = StringUtil::strtoull("012345678", out, 8);
  EXPECT_NE(nullptr, rest);
  EXPECT_EQ('8', *rest);
  EXPECT_EQ(01234567U, out);

  // Binary
  rest = StringUtil::strtoull("01010101012", out, 2);
  EXPECT_NE(nullptr, rest);
  EXPECT_EQ('2', *rest);
  EXPECT_EQ(0b101010101U, out);

  // Verify subsequent call to strtoull succeeds after the first one
  // failed due to errno ERANGE
  EXPECT_EQ(nullptr, StringUtil::strtoull("18446744073709551616", out));
  EXPECT_NE(nullptr, StringUtil::strtoull("18446744073709551615", out));
  EXPECT_EQ(18446744073709551615U, out);
}

TEST(StringUtil, atoull) {
  uint64_t out;
  EXPECT_FALSE(StringUtil::atoull("123b", out));
  EXPECT_FALSE(StringUtil::atoull("", out));
  EXPECT_FALSE(StringUtil::atoull("b123", out));

  EXPECT_TRUE(StringUtil::atoull("123", out));
  EXPECT_EQ(123U, out);

  EXPECT_TRUE(StringUtil::atoull("  456", out));
  EXPECT_EQ(456U, out);

  EXPECT_TRUE(StringUtil::atoull("00789", out));
  EXPECT_EQ(789U, out);

  // Verify subsequent call to atoull succeeds after the first one
  // failed due to errno ERANGE
  EXPECT_FALSE(StringUtil::atoull("18446744073709551616", out));
  EXPECT_TRUE(StringUtil::atoull("18446744073709551615", out));
  EXPECT_EQ(18446744073709551615U, out);
}

TEST(StringUtil, hasEmptySpace) {
  EXPECT_FALSE(StringUtil::hasEmptySpace("1234567890_-+=][|\"&*^%$#@!"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233 789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\t789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\f789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\v789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\n789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\r789"));

  EXPECT_TRUE(StringUtil::hasEmptySpace("1233 \t\f789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\v\n\r789"));
  EXPECT_TRUE(StringUtil::hasEmptySpace("1233\f\v\n789"));
}

TEST(StringUtil, replaceAllEmptySpace) {
  EXPECT_EQ("1234567890_-+=][|\"&*^%$#@!",
            StringUtil::replaceAllEmptySpace("1234567890_-+=][|\"&*^%$#@!"));
  EXPECT_EQ("1233_789", StringUtil::replaceAllEmptySpace("1233 789"));
  EXPECT_EQ("1233_789", StringUtil::replaceAllEmptySpace("1233\t789"));
  EXPECT_EQ("1233_789", StringUtil::replaceAllEmptySpace("1233\f789"));
  EXPECT_EQ("1233_789", StringUtil::replaceAllEmptySpace("1233\v789"));
  EXPECT_EQ("1233_789", StringUtil::replaceAllEmptySpace("1233\n789"));
  EXPECT_EQ("1233_789", StringUtil::replaceAllEmptySpace("1233\r789"));

  EXPECT_EQ("1233___789", StringUtil::replaceAllEmptySpace("1233 \t\f789"));
  EXPECT_EQ("1233___789", StringUtil::replaceAllEmptySpace("1233\v\n\r789"));
  EXPECT_EQ("1233___789", StringUtil::replaceAllEmptySpace("1233\f\v\n789"));
}

TEST(DateUtil, All) {
  EXPECT_FALSE(DateUtil::timePointValid(SystemTime()));
  DangerousDeprecatedTestTime test_time;
  EXPECT_TRUE(DateUtil::timePointValid(test_time.timeSystem().systemTime()));
}

TEST(DateUtil, NowToMilliseconds) {
  Event::SimulatedTimeSystem test_time;
  const SystemTime time_with_millis(std::chrono::seconds(12345) + std::chrono::milliseconds(67));
  test_time.setSystemTime(time_with_millis);
  EXPECT_EQ(12345067, DateUtil::nowToMilliseconds(test_time));
}

TEST(OutputBufferStream, FailsOnWriteToEmptyBuffer) {
  constexpr char data = 'x';
  OutputBufferStream ostream{nullptr, 0};
  ASSERT_TRUE(ostream.good());

  ostream << data;

  EXPECT_TRUE(ostream.bad());
}

TEST(OutputBufferStream, CanWriteToBuffer) {
  constexpr char data[] = "123";
  std::array<char, 3> buffer;

  OutputBufferStream ostream{buffer.data(), buffer.size()};
  ASSERT_EQ(ostream.bytesWritten(), 0);

  ostream << data;

  EXPECT_EQ(ostream.contents(), data);
  EXPECT_EQ(ostream.bytesWritten(), 3);
}

TEST(OutputBufferStream, CannotOverwriteBuffer) {
  constexpr char data[] = "123";
  std::array<char, 2> buffer;

  OutputBufferStream ostream{buffer.data(), buffer.size()};
  ASSERT_EQ(ostream.bytesWritten(), 0);

  // Initial write should stop before overflowing.
  ostream << data << std::endl;
  EXPECT_EQ(ostream.contents(), "12");
  EXPECT_EQ(ostream.bytesWritten(), 2);

  // Try a subsequent write, which shouldn't change anything since
  // the buffer is full.
  ostream << data << std::endl;
  EXPECT_EQ(ostream.contents(), "12");
  EXPECT_EQ(ostream.bytesWritten(), 2);
}

TEST(OutputBufferStream, DoesNotAllocateMemoryEvenIfWeTryToOverflowBuffer) {
  constexpr char data[] = "123";
  std::array<char, 2> buffer;
  Stats::TestUtil::MemoryTest memory_test;

  OutputBufferStream ostream{buffer.data(), buffer.size()};
  ostream << data << std::endl;

  EXPECT_EQ(memory_test.consumedBytes(), 0);
  EXPECT_EQ(ostream.contents(), "12");
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
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars.data(), ' '));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars.data(), '\t'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars.data(), '\f'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars.data(), '\v'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars.data(), '\n'));
  EXPECT_NE(nullptr, strchr(StringUtil::WhitespaceChars.data(), '\r'));
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

TEST(StringUtil, escape) {
  EXPECT_EQ(StringUtil::escape("hello world"), "hello world");
  EXPECT_EQ(StringUtil::escape("hello\nworld\n"), "hello\\nworld\\n");
  EXPECT_EQ(StringUtil::escape("\t\nworld\r\n"), "\\t\\nworld\\r\\n");
  EXPECT_EQ(StringUtil::escape("{\"linux\": \"penguin\"}"), "{\\\"linux\\\": \\\"penguin\\\"}");
}

TEST(StringUtil, escapeToOstream) {
  {
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, "hello world");
    EXPECT_EQ(ostream.contents(), "hello world");
  }

  {
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, "hello\nworld\n");
    EXPECT_EQ(ostream.contents(), "hello\\nworld\\n");
  }

  {
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, "\t\nworld\r\n");
    EXPECT_EQ(ostream.contents(), "\\t\\nworld\\r\\n");
  }

  {
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, "{'linux': \"penguin\"}");
    EXPECT_EQ(ostream.contents(), "{\\'linux\\': \\\"penguin\\\"}");
  }

  {
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, R"(\\)");
    EXPECT_EQ(ostream.contents(), R"(\\\\)");
  }

  {
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, "vertical\vtab");
    EXPECT_EQ(ostream.contents(), "vertical\\vtab");
  }

  {
    using namespace std::string_literals;
    std::array<char, 64> buffer;
    OutputBufferStream ostream{buffer.data(), buffer.size()};
    StringUtil::escapeToOstream(ostream, "null\0char"s);
    EXPECT_EQ(ostream.contents(), "null\\0char");
  }
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

TEST(StringUtil, RemoveTrailingCharacters) {
  EXPECT_EQ("", StringUtil::removeTrailingCharacters("......", '.'));
  EXPECT_EQ("\t\f\v\n\rhello ", StringUtil::removeTrailingCharacters("\t\f\v\n\rhello ", '.'));
  EXPECT_EQ("\t\f\v\n\r a b", StringUtil::removeTrailingCharacters("\t\f\v\n\r a b.......", '.'));
  EXPECT_EQ("", StringUtil::removeTrailingCharacters("", '.'));
}

TEST(StringUtil, StringViewTrim) {
  EXPECT_EQ("", StringUtil::trim("   "));
  EXPECT_EQ("hello", StringUtil::trim("\t\f\v\n\r  hello   "));
  EXPECT_EQ("he llo", StringUtil::trim(" \t\f\v\n\r he llo \t\f\v\n\r"));
}

TEST(StringUtil, StringViewCaseFindToken) {
  EXPECT_TRUE(StringUtil::caseFindToken("hello; world", ";", "HELLO"));
  EXPECT_FALSE(StringUtil::caseFindToken("hello; world", ";", "TEST"));
  EXPECT_TRUE(StringUtil::caseFindToken("heLLo; world", ";", "hello"));
  EXPECT_TRUE(StringUtil::caseFindToken("hello; world", ";", "hello"));
  EXPECT_FALSE(StringUtil::caseFindToken("hello; world", ".", "hello"));
  EXPECT_TRUE(StringUtil::caseFindToken("", ",", ""));
  EXPECT_FALSE(StringUtil::caseFindToken("", "", "a"));
  EXPECT_TRUE(StringUtil::caseFindToken(" ", " ", "", true));
  EXPECT_FALSE(StringUtil::caseFindToken(" ", " ", "", false));
  EXPECT_TRUE(StringUtil::caseFindToken("A=5", ".", "A=5"));
}

TEST(StringUtil, StringViewCropRight) {
  EXPECT_EQ("hello", StringUtil::cropRight("hello; world\t\f\v\n\r", ";"));
  EXPECT_EQ("foo ", StringUtil::cropRight("foo ; ; ; ; ; ; ", ";"));
  EXPECT_EQ("", StringUtil::cropRight(";hello world\t\f\v\n\r", ";"));
  EXPECT_EQ(" hel", StringUtil::cropRight(" hello alo\t\f\v\n\r", "lo"));
  EXPECT_EQ("\t\f\v\n\rhe 1", StringUtil::cropRight("\t\f\v\n\rhe 12\t\f\v\n\r", "2"));
  EXPECT_EQ("hello", StringUtil::cropRight("hello alo\t\f\v\n\r", " a"));
  EXPECT_EQ("hello ", StringUtil::cropRight("hello alo\t\f\v\n\r", "a"));
  EXPECT_EQ("abcd", StringUtil::cropRight("abcd", ";"));
}

TEST(StringUtil, StringViewCropLeft) {
  EXPECT_EQ(" world\t\f\v\n\r", StringUtil::cropLeft("hello; world\t\f\v\n\r", ";"));
  EXPECT_EQ("hello world ", StringUtil::cropLeft(";hello world ", ";"));
  EXPECT_EQ("\t\f\v\n\ralo", StringUtil::cropLeft("\t\f\v\n\rhello\t\f\v\n\ralo", "lo"));
  EXPECT_EQ("2\t\f\v\n\r", StringUtil::cropLeft("\t\f\v\n\rhe 12\t\f\v\n\r", "1"));
  EXPECT_EQ("lo\t\f\v\n\r", StringUtil::cropLeft("hello alo\t\f\v\n\r", " a"));
  EXPECT_EQ(" ; ; ; ; ", StringUtil::cropLeft("foo ; ; ; ; ; ", ";"));
  EXPECT_EQ("abcd", StringUtil::cropLeft("abcd", ";"));
  EXPECT_EQ("", StringUtil::cropLeft("abcd", "abcd"));
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

TEST(StringUtil, StringViewCaseInsensitiveHash) {
  EXPECT_EQ(13876786532495509697U, StringUtil::CaseInsensitiveHash()("hello world"));
}

TEST(StringUtil, StringViewCaseInsensitiveCompare) {
  EXPECT_TRUE(StringUtil::CaseInsensitiveCompare()("hello world", "hello world"));
  EXPECT_TRUE(StringUtil::CaseInsensitiveCompare()("HELLO world", "hello world"));
  EXPECT_FALSE(StringUtil::CaseInsensitiveCompare()("hello!", "hello world"));
}

TEST(StringUtil, StringViewCaseUnorderedSet) {
  StringUtil::CaseUnorderedSet words{"Test", "hello", "WORLD", "Test"};
  EXPECT_EQ(3, words.size());
  EXPECT_EQ("Test", *(words.find("test")));
  EXPECT_EQ("hello", *(words.find("HELLO")));
  EXPECT_EQ("WORLD", *(words.find("world")));
  EXPECT_EQ(words.end(), words.find("hello world"));
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
  {
    auto tokens = StringUtil::splitToken(" one , two , three ", ",", true, true);
    EXPECT_EQ(3, tokens.size());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "one") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "two") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "three") != tokens.end());
  }
  {
    auto tokens = StringUtil::splitToken(" one ,  , three=five ", ",=", true, true);
    EXPECT_EQ(4, tokens.size());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "one") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "three") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "five") != tokens.end());
  }
  {
    auto tokens = StringUtil::splitToken(" one ,  , three=five ", ",=", false, true);
    EXPECT_EQ(3, tokens.size());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "one") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "three") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "five") != tokens.end());
  }
  {
    auto tokens = StringUtil::splitToken(" one ,  , three=five ", ",=", false);
    EXPECT_EQ(4, tokens.size());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " one ") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "  ") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), " three") != tokens.end());
    EXPECT_TRUE(std::find(tokens.begin(), tokens.end(), "five ") != tokens.end());
  }
}

TEST(StringUtil, StringViewRemoveTokens) {
  // Basic cases.
  EXPECT_EQ(StringUtil::removeTokens("", ",", {"two"}, ","), "");
  EXPECT_EQ(StringUtil::removeTokens("one", ",", {"two"}, ","), "one");
  EXPECT_EQ(StringUtil::removeTokens("one,two ", ",", {"two"}, ","), "one");
  EXPECT_EQ(StringUtil::removeTokens("one,two ", ",", {"two", "one"}, ","), "");
  EXPECT_EQ(StringUtil::removeTokens("one,two ", ",", {"one"}, ","), "two");
  EXPECT_EQ(StringUtil::removeTokens("one,two,three ", ",", {"two"}, ","), "one,three");
  EXPECT_EQ(StringUtil::removeTokens(" one , two , three ", ",", {"two"}, ","), "one,three");
  EXPECT_EQ(StringUtil::removeTokens(" one , two , three ", ",", {"three"}, ","), "one,two");
  EXPECT_EQ(StringUtil::removeTokens(" one , two , three ", ",", {"three"}, ", "), "one, two");
  EXPECT_EQ(StringUtil::removeTokens("one,two,three", ",", {"two", "three"}, ","), "one");
  EXPECT_EQ(StringUtil::removeTokens("one,two,three,four", ",", {"two", "three"}, ","), "one,four");
  // Ignore case.
  EXPECT_EQ(StringUtil::removeTokens("One,Two,Three,Four", ",", {"two", "three"}, ","), "One,Four");
  // Longer joiner.
  EXPECT_EQ(StringUtil::removeTokens("one,two,three,four", ",", {"two", "three"}, " , "),
            "one , four");
  // Delimiters.
  EXPECT_EQ(StringUtil::removeTokens("one,two;three ", ",;", {"two"}, ","), "one,three");
}

TEST(StringUtil, removeCharacters) {
  IntervalSetImpl<size_t> removals;
  removals.insert(3, 5);
  removals.insert(7, 10);
  EXPECT_EQ("01256", StringUtil::removeCharacters("0123456789", removals));
  removals.insert(0, 1);
  EXPECT_EQ("1256x", StringUtil::removeCharacters("0123456789x", removals));
}

TEST(AccessLogDateTimeFormatter, fromTime) {
  SystemTime time1(std::chrono::seconds(1522796769));
  EXPECT_EQ("2018-04-03T23:06:09.000Z", AccessLogDateTimeFormatter::fromTime(time1));
  SystemTime time2(std::chrono::milliseconds(1522796769123));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", AccessLogDateTimeFormatter::fromTime(time2));
  SystemTime time3(std::chrono::milliseconds(1522796769999));
  EXPECT_EQ("2018-04-03T23:06:09.999Z", AccessLogDateTimeFormatter::fromTime(time3));
  SystemTime time4(std::chrono::milliseconds(1522796768999));
  EXPECT_EQ("2018-04-03T23:06:08.999Z", AccessLogDateTimeFormatter::fromTime(time4));
}

TEST(Primes, isPrime) {
  EXPECT_FALSE(Primes::isPrime(0));
  EXPECT_TRUE(Primes::isPrime(67));
  EXPECT_FALSE(Primes::isPrime(49));
  EXPECT_FALSE(Primes::isPrime(102));
  EXPECT_TRUE(Primes::isPrime(103));
}

TEST(Primes, findPrimeLargerThan) {
  EXPECT_EQ(1, Primes::findPrimeLargerThan(0));
  EXPECT_EQ(67, Primes::findPrimeLargerThan(62));
  EXPECT_EQ(107, Primes::findPrimeLargerThan(103));
  EXPECT_EQ(10007, Primes::findPrimeLargerThan(9991));
}

class WeightedClusterEntry {
public:
  WeightedClusterEntry(const std::string name, const uint64_t weight)
      : name_(name), weight_(weight) {}

  const std::string& clusterName() const { return name_; }
  uint64_t clusterWeight() const { return weight_; }

private:
  const std::string name_;
  const uint64_t weight_;
};
using WeightedClusterEntrySharedPtr = std::shared_ptr<WeightedClusterEntry>;

TEST(WeightedClusterUtil, pickCluster) {
  std::vector<WeightedClusterEntrySharedPtr> clusters;

  std::unique_ptr<WeightedClusterEntry> cluster1(new WeightedClusterEntry("cluster1", 10));
  clusters.emplace_back(std::move(cluster1));

  std::unique_ptr<WeightedClusterEntry> cluster2(new WeightedClusterEntry("cluster2", 90));
  clusters.emplace_back(std::move(cluster2));

  EXPECT_EQ("cluster1", WeightedClusterUtil::pickCluster(clusters, 100, 5, false)->clusterName());
  EXPECT_EQ("cluster2", WeightedClusterUtil::pickCluster(clusters, 80, 79, true)->clusterName());
}

static std::string intervalSetIntToString(const IntervalSetImpl<int>& interval_set) {
  std::string out;
  const char* prefix = "";
  for (const auto& interval : interval_set.toVector()) {
    absl::StrAppend(&out, prefix, "[", interval.first, ", ", interval.second, ")");
    prefix = ", ";
  }
  return out;
}

TEST(IntervalSet, testIntervalAccumulation) {
  IntervalSetImpl<int> interval_set;
  auto insert_and_print = [&interval_set](int left, int right) -> std::string {
    interval_set.insert(left, right);
    return intervalSetIntToString(interval_set);
  };
  EXPECT_EQ("[7, 10)", insert_and_print(7, 10));
  EXPECT_EQ("[-2, -1), [7, 10)", insert_and_print(-2, -1));           // disjoint left
  EXPECT_EQ("[-2, -1), [7, 10), [22, 23)", insert_and_print(22, 23)); // disjoint right
  EXPECT_EQ("[-2, -1), [7, 15), [22, 23)", insert_and_print(8, 15));  // right overhang
  EXPECT_EQ("[-2, -1), [5, 15), [22, 23)", insert_and_print(5, 12));  // left overhang
  EXPECT_EQ("[-2, -1), [5, 15), [22, 23)", insert_and_print(3, 3));   // empty; no change
  EXPECT_EQ("[-2, -1), [3, 4), [5, 15), [22, 23)",                    // single-element add
            insert_and_print(3, 4));
  EXPECT_EQ("[-2, -1), [2, 4), [5, 15), [22, 23)", // disjoint in middle
            insert_and_print(2, 4));
  EXPECT_EQ("[-2, -1), [2, 15), [22, 23)", insert_and_print(3, 6)); // merge two intervals
  EXPECT_EQ("[-2, -1), [2, 15), [18, 19), [22, 23)",                // right disjoint
            insert_and_print(18, 19));
  EXPECT_EQ("[-2, -1), [2, 15), [16, 17), [18, 19), [22, 23)", // middle disjoint
            insert_and_print(16, 17));
  EXPECT_EQ("[-2, -1), [2, 15), [16, 17), [18, 20), [22, 23)", // merge [18,19) and [19,20)
            insert_and_print(19, 20));
  EXPECT_EQ("[-2, -1), [2, 15), [16, 17), [18, 20), [22, 23)", // fully enclosed; no effect
            insert_and_print(3, 6));
  EXPECT_EQ("[-2, -1), [2, 20), [22, 23)", insert_and_print(3, 20)); // merge across 3 intervals
  EXPECT_EQ("[-2, -1), [2, 23)", insert_and_print(3, 22));           // merge all via overlap
  EXPECT_EQ("[-2, 23)", insert_and_print(-2, 23));                   // merge all covering exact
  EXPECT_EQ("[-3, 24)", insert_and_print(-3, 24)); // merge all with overhand on both sides

  interval_set.clear();
  EXPECT_EQ("", insert_and_print(10, 10));
  EXPECT_EQ("[25, 26)", insert_and_print(25, 26));
  EXPECT_EQ("[5, 11), [25, 26)", insert_and_print(5, 11));
}

TEST(IntervalSet, testIntervalTargeted) {
  auto test = [](int left, int right) -> std::string {
    IntervalSetImpl<int> interval_set;
    interval_set.insert(15, 20);
    interval_set.insert(25, 30);
    interval_set.insert(35, 40);
    interval_set.insert(left, right);
    return intervalSetIntToString(interval_set);
  };

  // There are 3 spans, and there are 19 potentially interesting slots
  // for each coordinate, with the constraint that each left < right.
  // We'll do one test that left==right has no effect first. So there's
  // about 19^2/2 = 180 combinations, which is a lot but not too bad. Of
  // course many of these are essentially the same case but it's worth making
  // sure there's no problems in corner cases.
  //
  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:  x   x   x   xxx  x  x   x  xxx  x  x   x  xxx x

  // First the corner-case of an empty insertion, leaving the input unchanged.
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(2, 2));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:  [)
  EXPECT_EQ("[2, 3), [15, 20), [25, 30), [35, 40)", test(2, 3));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:  [   )   )   )))  )  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[2, 20), [25, 30), [35, 40)", test(2, 15));
  EXPECT_EQ("[2, 20), [25, 30), [35, 40)", test(2, 17));
  EXPECT_EQ("[2, 20), [25, 30), [35, 40)", test(2, 19));
  EXPECT_EQ("[2, 20), [25, 30), [35, 40)", test(2, 20));
  EXPECT_EQ("[2, 21), [25, 30), [35, 40)", test(2, 21));
  EXPECT_EQ("[2, 23), [25, 30), [35, 40)", test(2, 23));
  EXPECT_EQ("[2, 30), [35, 40)", test(2, 25));
  EXPECT_EQ("[2, 30), [35, 40)", test(2, 27));
  EXPECT_EQ("[2, 30), [35, 40)", test(2, 29));
  EXPECT_EQ("[2, 30), [35, 40)", test(2, 30));
  EXPECT_EQ("[2, 31), [35, 40)", test(2, 31));
  EXPECT_EQ("[2, 33), [35, 40)", test(2, 33));
  EXPECT_EQ("[2, 40)", test(2, 35));
  EXPECT_EQ("[2, 40)", test(2, 37));
  EXPECT_EQ("[2, 40)", test(2, 39));
  EXPECT_EQ("[2, 40)", test(2, 40));
  EXPECT_EQ("[2, 41)", test(2, 41));
  EXPECT_EQ("[2, 43)", test(2, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:      [   )   )))  )  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(15, 17));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(15, 19));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(15, 20));
  EXPECT_EQ("[15, 21), [25, 30), [35, 40)", test(15, 21));
  EXPECT_EQ("[15, 23), [25, 30), [35, 40)", test(15, 23));
  EXPECT_EQ("[15, 30), [35, 40)", test(15, 25));
  EXPECT_EQ("[15, 30), [35, 40)", test(15, 27));
  EXPECT_EQ("[15, 30), [35, 40)", test(15, 29));
  EXPECT_EQ("[15, 30), [35, 40)", test(15, 30));
  EXPECT_EQ("[15, 31), [35, 40)", test(15, 31));
  EXPECT_EQ("[15, 33), [35, 40)", test(15, 33));
  EXPECT_EQ("[15, 40)", test(15, 35));
  EXPECT_EQ("[15, 40)", test(15, 37));
  EXPECT_EQ("[15, 40)", test(15, 39));
  EXPECT_EQ("[15, 40)", test(15, 40));
  EXPECT_EQ("[15, 41)", test(15, 41));
  EXPECT_EQ("[15, 43)", test(15, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:          [   )))  )  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(17, 19));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(17, 20));
  EXPECT_EQ("[15, 21), [25, 30), [35, 40)", test(17, 21));
  EXPECT_EQ("[15, 23), [25, 30), [35, 40)", test(17, 23));
  EXPECT_EQ("[15, 30), [35, 40)", test(17, 25));
  EXPECT_EQ("[15, 30), [35, 40)", test(17, 27));
  EXPECT_EQ("[15, 30), [35, 40)", test(17, 29));
  EXPECT_EQ("[15, 30), [35, 40)", test(17, 30));
  EXPECT_EQ("[15, 31), [35, 40)", test(17, 31));
  EXPECT_EQ("[15, 33), [35, 40)", test(17, 33));
  EXPECT_EQ("[15, 40)", test(17, 35));
  EXPECT_EQ("[15, 40)", test(17, 37));
  EXPECT_EQ("[15, 40)", test(17, 39));
  EXPECT_EQ("[15, 40)", test(17, 40));
  EXPECT_EQ("[15, 41)", test(17, 41));
  EXPECT_EQ("[15, 43)", test(17, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:              [))  )  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(19, 20));
  EXPECT_EQ("[15, 21), [25, 30), [35, 40)", test(19, 21));
  EXPECT_EQ("[15, 23), [25, 30), [35, 40)", test(19, 23));
  EXPECT_EQ("[15, 30), [35, 40)", test(19, 25));
  EXPECT_EQ("[15, 30), [35, 40)", test(19, 27));
  EXPECT_EQ("[15, 30), [35, 40)", test(19, 29));
  EXPECT_EQ("[15, 30), [35, 40)", test(19, 30));
  EXPECT_EQ("[15, 31), [35, 40)", test(19, 31));
  EXPECT_EQ("[15, 33), [35, 40)", test(19, 33));
  EXPECT_EQ("[15, 40)", test(19, 35));
  EXPECT_EQ("[15, 40)", test(19, 37));
  EXPECT_EQ("[15, 40)", test(19, 39));
  EXPECT_EQ("[15, 40)", test(19, 40));
  EXPECT_EQ("[15, 41)", test(19, 41));
  EXPECT_EQ("[15, 43)", test(19, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:               [)  )  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 21), [25, 30), [35, 40)", test(20, 21));
  EXPECT_EQ("[15, 23), [25, 30), [35, 40)", test(20, 23));
  EXPECT_EQ("[15, 30), [35, 40)", test(20, 25));
  EXPECT_EQ("[15, 30), [35, 40)", test(20, 27));
  EXPECT_EQ("[15, 30), [35, 40)", test(20, 29));
  EXPECT_EQ("[15, 30), [35, 40)", test(20, 30));
  EXPECT_EQ("[15, 31), [35, 40)", test(20, 31));
  EXPECT_EQ("[15, 33), [35, 40)", test(20, 33));
  EXPECT_EQ("[15, 40)", test(20, 35));
  EXPECT_EQ("[15, 40)", test(20, 37));
  EXPECT_EQ("[15, 40)", test(20, 39));
  EXPECT_EQ("[15, 40)", test(20, 40));
  EXPECT_EQ("[15, 41)", test(20, 41));
  EXPECT_EQ("[15, 43)", test(20, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                [  )  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [21, 23), [25, 30), [35, 40)", test(21, 23));
  EXPECT_EQ("[15, 20), [21, 30), [35, 40)", test(21, 25));
  EXPECT_EQ("[15, 20), [21, 30), [35, 40)", test(21, 27));
  EXPECT_EQ("[15, 20), [21, 30), [35, 40)", test(21, 29));
  EXPECT_EQ("[15, 20), [21, 30), [35, 40)", test(21, 30));
  EXPECT_EQ("[15, 20), [21, 31), [35, 40)", test(21, 31));
  EXPECT_EQ("[15, 20), [21, 33), [35, 40)", test(21, 33));
  EXPECT_EQ("[15, 20), [21, 40)", test(21, 35));
  EXPECT_EQ("[15, 20), [21, 40)", test(21, 37));
  EXPECT_EQ("[15, 20), [21, 40)", test(21, 39));
  EXPECT_EQ("[15, 20), [21, 40)", test(21, 40));
  EXPECT_EQ("[15, 20), [21, 41)", test(21, 41));
  EXPECT_EQ("[15, 20), [21, 43)", test(21, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                   [  )   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [23, 30), [35, 40)", test(23, 25));
  EXPECT_EQ("[15, 20), [23, 30), [35, 40)", test(23, 27));
  EXPECT_EQ("[15, 20), [23, 30), [35, 40)", test(23, 29));
  EXPECT_EQ("[15, 20), [23, 30), [35, 40)", test(23, 30));
  EXPECT_EQ("[15, 20), [23, 31), [35, 40)", test(23, 31));
  EXPECT_EQ("[15, 20), [23, 33), [35, 40)", test(23, 33));
  EXPECT_EQ("[15, 20), [23, 40)", test(23, 35));
  EXPECT_EQ("[15, 20), [23, 40)", test(23, 37));
  EXPECT_EQ("[15, 20), [23, 40)", test(23, 39));
  EXPECT_EQ("[15, 20), [23, 40)", test(23, 40));
  EXPECT_EQ("[15, 20), [23, 41)", test(23, 41));
  EXPECT_EQ("[15, 20), [23, 43)", test(23, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                      [   )  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(25, 27));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(25, 29));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(25, 30));
  EXPECT_EQ("[15, 20), [25, 31), [35, 40)", test(25, 31));
  EXPECT_EQ("[15, 20), [25, 33), [35, 40)", test(25, 33));
  EXPECT_EQ("[15, 20), [25, 40)", test(25, 35));
  EXPECT_EQ("[15, 20), [25, 40)", test(25, 37));
  EXPECT_EQ("[15, 20), [25, 40)", test(25, 39));
  EXPECT_EQ("[15, 20), [25, 40)", test(25, 40));
  EXPECT_EQ("[15, 20), [25, 41)", test(25, 41));
  EXPECT_EQ("[15, 20), [25, 43)", test(25, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                          [  )))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(27, 29));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(27, 30));
  EXPECT_EQ("[15, 20), [25, 31), [35, 40)", test(27, 31));
  EXPECT_EQ("[15, 20), [25, 33), [35, 40)", test(27, 33));
  EXPECT_EQ("[15, 20), [25, 40)", test(27, 35));
  EXPECT_EQ("[15, 20), [25, 40)", test(27, 37));
  EXPECT_EQ("[15, 20), [25, 40)", test(27, 39));
  EXPECT_EQ("[15, 20), [25, 40)", test(27, 40));
  EXPECT_EQ("[15, 20), [25, 41)", test(27, 41));
  EXPECT_EQ("[15, 20), [25, 43)", test(27, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                             [))  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(29, 30));
  EXPECT_EQ("[15, 20), [25, 31), [35, 40)", test(29, 31));
  EXPECT_EQ("[15, 20), [25, 33), [35, 40)", test(29, 33));
  EXPECT_EQ("[15, 20), [25, 40)", test(29, 35));
  EXPECT_EQ("[15, 20), [25, 40)", test(29, 37));
  EXPECT_EQ("[15, 20), [25, 40)", test(29, 39));
  EXPECT_EQ("[15, 20), [25, 40)", test(29, 40));
  EXPECT_EQ("[15, 20), [25, 41)", test(29, 41));
  EXPECT_EQ("[15, 20), [25, 43)", test(29, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                              [)  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 31), [35, 40)", test(30, 31));
  EXPECT_EQ("[15, 20), [25, 33), [35, 40)", test(30, 33));
  EXPECT_EQ("[15, 20), [25, 40)", test(30, 35));
  EXPECT_EQ("[15, 20), [25, 40)", test(30, 37));
  EXPECT_EQ("[15, 20), [25, 40)", test(30, 39));
  EXPECT_EQ("[15, 20), [25, 40)", test(30, 40));
  EXPECT_EQ("[15, 20), [25, 41)", test(30, 41));
  EXPECT_EQ("[15, 20), [25, 43)", test(30, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                               [  )  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [31, 33), [35, 40)", test(31, 33));
  EXPECT_EQ("[15, 20), [25, 30), [31, 40)", test(31, 35));
  EXPECT_EQ("[15, 20), [25, 30), [31, 40)", test(31, 37));
  EXPECT_EQ("[15, 20), [25, 30), [31, 40)", test(31, 39));
  EXPECT_EQ("[15, 20), [25, 30), [31, 40)", test(31, 40));
  EXPECT_EQ("[15, 20), [25, 30), [31, 41)", test(31, 41));
  EXPECT_EQ("[15, 20), [25, 30), [31, 43)", test(31, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                                  [  )   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [33, 40)", test(33, 35));
  EXPECT_EQ("[15, 20), [25, 30), [33, 40)", test(33, 37));
  EXPECT_EQ("[15, 20), [25, 30), [33, 40)", test(33, 39));
  EXPECT_EQ("[15, 20), [25, 30), [33, 40)", test(33, 40));
  EXPECT_EQ("[15, 20), [25, 30), [33, 41)", test(33, 41));
  EXPECT_EQ("[15, 20), [25, 30), [33, 43)", test(33, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                                     [   )  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(35, 37));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(35, 39));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(35, 40));
  EXPECT_EQ("[15, 20), [25, 30), [35, 41)", test(35, 41));
  EXPECT_EQ("[15, 20), [25, 30), [35, 43)", test(35, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                                         [  ))) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(37, 39));
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(37, 40));
  EXPECT_EQ("[15, 20), [25, 30), [35, 41)", test(37, 41));
  EXPECT_EQ("[15, 20), [25, 30), [35, 43)", test(37, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                                            [)) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40)", test(39, 40));
  EXPECT_EQ("[15, 20), [25, 30), [35, 41)", test(39, 41));
  EXPECT_EQ("[15, 20), [25, 30), [35, 43)", test(39, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                                             [) )
  EXPECT_EQ("[15, 20), [25, 30), [35, 41)", test(40, 41));
  EXPECT_EQ("[15, 20), [25, 30), [35, 43)", test(40, 43));

  // initial setup:         [15    20)      [25   30)      [35   35)
  // insertion points:                                              [ )
  EXPECT_EQ("[15, 20), [25, 30), [35, 40), [41, 43)", test(41, 43));
}

TEST(IntervalSet, testTest) {
  IntervalSetImpl<uint32_t> set;
  set.insert(4, 6);
  EXPECT_FALSE(set.test(0));
  set.insert(0, 2);
  EXPECT_TRUE(set.test(0));
  EXPECT_TRUE(set.test(1));
  EXPECT_FALSE(set.test(2));
  EXPECT_FALSE(set.test(3));
  EXPECT_TRUE(set.test(4));
  EXPECT_TRUE(set.test(5));
  EXPECT_FALSE(set.test(6));
  EXPECT_FALSE(set.test(7));
}

TEST(IntervalSet, testTestDouble) {
  IntervalSetImpl<double> set;
  set.insert(4.0, 6.0);
  EXPECT_FALSE(set.test(0));
  EXPECT_FALSE(set.test(3.9999));
  EXPECT_TRUE(set.test(4.0));
  EXPECT_TRUE(set.test(4.0001));
  EXPECT_TRUE(set.test(5.9999));
  EXPECT_FALSE(set.test(6.0));
  set.insert(0, 2);
  EXPECT_TRUE(set.test(0));
  EXPECT_TRUE(set.test(1));
  EXPECT_TRUE(set.test(1.999));
  EXPECT_FALSE(set.test(3));
  EXPECT_TRUE(set.test(4));
  EXPECT_TRUE(set.test(5));
  EXPECT_FALSE(set.test(6));
  EXPECT_FALSE(set.test(7));
}

TEST(WelfordStandardDeviation, AllEntriesTheSame) {
  WelfordStandardDeviation wsd;
  wsd.update(10);
  wsd.update(10);
  wsd.update(10);
  EXPECT_EQ(10, wsd.mean());
  EXPECT_EQ(0, wsd.computeStandardDeviation());
}

TEST(WelfordStandardDeviation, SmallVariance) {
  WelfordStandardDeviation wsd;
  wsd.update(10);
  wsd.update(10);
  wsd.update(10);
  wsd.update(9);
  wsd.update(11);
  EXPECT_LT(0.5, wsd.computeStandardDeviation());
  EXPECT_GT(1.0, wsd.computeStandardDeviation());
  EXPECT_EQ(10, wsd.mean());
}

TEST(WelfordStandardDeviation, HugeVariance) {
  WelfordStandardDeviation wsd;
  wsd.update(20);
  wsd.update(2000);
  wsd.update(200000);
  wsd.update(20000000);
  EXPECT_EQ(5050505, wsd.mean());
  EXPECT_LT(1000, wsd.computeStandardDeviation());
}

TEST(WelfordStandardDeviation, InsufficientData) {
  WelfordStandardDeviation wsd;
  wsd.update(10);
  EXPECT_EQ(10, wsd.mean());
  EXPECT_TRUE(std::isnan(wsd.computeStandardDeviation()));
}

TEST(DateFormatter, FromTime) {
  const SystemTime time1(std::chrono::seconds(1522796769));
  EXPECT_EQ("2018-04-03T23:06:09.000Z", DateFormatter("%Y-%m-%dT%H:%M:%S.000Z").fromTime(time1));
  EXPECT_EQ("aaa23", DateFormatter(std::string(3, 'a') + "%H").fromTime(time1));
  const SystemTime time2(std::chrono::seconds(0));
  EXPECT_EQ("1970-01-01T00:00:00.000Z", DateFormatter("%Y-%m-%dT%H:%M:%S.000Z").fromTime(time2));
  EXPECT_EQ("aaa00", DateFormatter(std::string(3, 'a') + "%H").fromTime(time2));
}

// Check the time complexity. Make sure DateFormatter can finish parsing long messy string without
// crashing/freezing. This should pass in 0-2 seconds if O(n). Finish in 30-120 seconds if O(n^2)
TEST(DateFormatter, ParseLongString) {
  std::string input;
  std::string expected_output;
  int num_duplicates = 400;
  std::string duplicate_input = "%%1f %1f, %2f, %3f, %4f, ";
  std::string duplicate_output = "%1 1, 14, 142, 1420, ";
  for (int i = 0; i < num_duplicates; i++) {
    absl::StrAppend(&input, duplicate_input, "(");
    absl::StrAppend(&expected_output, duplicate_output, "(");
  }
  absl::StrAppend(&input, duplicate_input);
  absl::StrAppend(&expected_output, duplicate_output);

  const SystemTime time1(std::chrono::seconds(1522796769) + std::chrono::milliseconds(142));
  std::string output = DateFormatter(input).fromTime(time1);
  EXPECT_EQ(expected_output, output);
}

// Verify that two DateFormatter patterns with the same ??? patterns but
// different format strings don't false share cache entries. This is a
// regression test for when they did.
TEST(DateFormatter, FromTimeSameWildcard) {
  const SystemTime time1(std::chrono::seconds(1522796769) + std::chrono::milliseconds(142));
  EXPECT_EQ("2018-04-03T23:06:09.000Z142",
            DateFormatter("%Y-%m-%dT%H:%M:%S.000Z%3f").fromTime(time1));
  EXPECT_EQ("2018-04-03T23:06:09.000Z114",
            DateFormatter("%Y-%m-%dT%H:%M:%S.000Z%1f%2f").fromTime(time1));
}

TEST(TrieLookupTable, AddItems) {
  TrieLookupTable<const char*> trie;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  EXPECT_TRUE(trie.add("foo", cstr_a));
  EXPECT_TRUE(trie.add("bar", cstr_b));
  EXPECT_EQ(cstr_a, trie.find("foo"));
  EXPECT_EQ(cstr_b, trie.find("bar"));

  // overwrite_existing = false
  EXPECT_FALSE(trie.add("foo", cstr_c, false));
  EXPECT_EQ(cstr_a, trie.find("foo"));

  // overwrite_existing = true
  EXPECT_TRUE(trie.add("foo", cstr_c));
  EXPECT_EQ(cstr_c, trie.find("foo"));
}

TEST(TrieLookupTable, LongestPrefix) {
  TrieLookupTable<const char*> trie;
  const char* cstr_a = "a";
  const char* cstr_b = "b";
  const char* cstr_c = "c";

  EXPECT_TRUE(trie.add("foo", cstr_a));
  EXPECT_TRUE(trie.add("bar", cstr_b));
  EXPECT_TRUE(trie.add("baro", cstr_c));

  EXPECT_EQ(cstr_a, trie.find("foo"));
  EXPECT_EQ(cstr_a, trie.findLongestPrefix("foo"));
  EXPECT_EQ(cstr_a, trie.findLongestPrefix("foosball"));

  EXPECT_EQ(cstr_b, trie.find("bar"));
  EXPECT_EQ(cstr_b, trie.findLongestPrefix("bar"));
  EXPECT_EQ(cstr_b, trie.findLongestPrefix("baritone"));
  EXPECT_EQ(cstr_c, trie.findLongestPrefix("barometer"));

  EXPECT_EQ(nullptr, trie.find("toto"));
  EXPECT_EQ(nullptr, trie.findLongestPrefix("toto"));
  EXPECT_EQ(nullptr, trie.find(" "));
  EXPECT_EQ(nullptr, trie.findLongestPrefix(" "));
}

TEST(InlineStorageTest, InlineString) {
  InlineStringPtr hello = InlineString::create("Hello, world!");
  EXPECT_EQ("Hello, world!", hello->toStringView());
  EXPECT_EQ("Hello, world!", hello->toString());
}

#ifdef WIN32
TEST(ErrorDetailsTest, WindowsFormatMessage) {
  // winsock2 error
  EXPECT_NE(errorDetails(SOCKET_ERROR_AGAIN), "");
  EXPECT_THAT(errorDetails(SOCKET_ERROR_AGAIN), Not(HasSubstr("\r\n")));
  EXPECT_NE(errorDetails(SOCKET_ERROR_AGAIN), "Unknown error");

  // winsock2 error with a long message
  EXPECT_NE(errorDetails(SOCKET_ERROR_MSG_SIZE), "");
  EXPECT_THAT(errorDetails(SOCKET_ERROR_MSG_SIZE), Not(HasSubstr("\r\n")));
  EXPECT_NE(errorDetails(SOCKET_ERROR_MSG_SIZE), "Unknown error");

  // regular Windows error
  EXPECT_NE(errorDetails(ERROR_FILE_NOT_FOUND), "");
  EXPECT_THAT(errorDetails(ERROR_FILE_NOT_FOUND), Not(HasSubstr("\r\n")));
  EXPECT_NE(errorDetails(ERROR_FILE_NOT_FOUND), "Unknown error");

  // invalid error code
  EXPECT_EQ(errorDetails(99999), "Unknown error");
}
#endif

TEST(SetUtil, All) {
  {
    absl::flat_hash_set<uint32_t> result;
    SetUtil::setDifference({1, 2, 3}, {1, 3}, result);
    EXPECT_THAT(result, WhenSorted(ElementsAre(2)));
  }
  {
    absl::flat_hash_set<uint32_t> result;
    SetUtil::setDifference({1, 2, 3}, {4, 5}, result);
    EXPECT_THAT(result, WhenSorted(ElementsAre(1, 2, 3)));
  }
  {
    absl::flat_hash_set<uint32_t> result;
    SetUtil::setDifference({}, {4, 5}, result);
    EXPECT_THAT(result, WhenSorted(ElementsAre()));
  }
  {
    absl::flat_hash_set<uint32_t> result;
    SetUtil::setDifference({1, 2, 3}, {}, result);
    EXPECT_THAT(result, WhenSorted(ElementsAre(1, 2, 3)));
  }
}

} // namespace Envoy
