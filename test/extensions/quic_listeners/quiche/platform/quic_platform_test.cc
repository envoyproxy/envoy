#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_aligned.h"
#include "quiche/quic/platform/api/quic_arraysize.h"
#include "quiche/quic/platform/api/quic_containers.h"
#include "quiche/quic/platform/api/quic_endian.h"
#include "quiche/quic/platform/api/quic_estimate_memory_usage.h"
#include "quiche/quic/platform/api/quic_string.h"
#include "quiche/quic/platform/api/quic_string_piece.h"
#include "quiche/quic/platform/api/quic_str_cat.h"

// Basic tests to validate functioning of the QUICHE quic platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

TEST(QuicPlatformTest, QuicAlignOf) { EXPECT_LT(0, QUIC_ALIGN_OF(int)); }

TEST(QuicPlatformTest, QuicArraysize) {
  int array[] = {0, 1, 2, 3, 4};
  EXPECT_EQ(5, QUIC_ARRAYSIZE(array));
}

TEST(QuicPlatformTest, QuicUnorderedMap) {
  quic::QuicUnorderedMap<quic::QuicString, int> umap;
  umap.insert({"foo", 2});
  EXPECT_EQ(2, umap["foo"]);
}

TEST(QuicPlatformTest, QuicUnorderedSet) {
  quic::QuicUnorderedSet<quic::QuicString> uset({"foo", "bar"});
  EXPECT_EQ(1, uset.count("bar"));
  EXPECT_EQ(0, uset.count("qux"));
}

TEST(QuicPlatformTest, QuicQueue) {
  quic::QuicQueue<int> queue;
  queue.push(10);
  EXPECT_EQ(10, queue.back());
}

TEST(QuicPlatformTest, QuicDeque) {
  quic::QuicDeque<int> deque;
  deque.push_back(10);
  EXPECT_EQ(10, deque.back());
}

TEST(QuicPlatformTest, QuicInlinedVector) {
  quic::QuicInlinedVector<int, 5> vec;
  vec.push_back(3);
  EXPECT_EQ(3, vec[0]);
}

TEST(QuicPlatformTest, QuicEndian) {
  EXPECT_EQ(0x1234, quic::QuicEndian::NetToHost16(quic::QuicEndian::HostToNet16(0x1234)));
  EXPECT_EQ(0x12345678, quic::QuicEndian::NetToHost32(quic::QuicEndian::HostToNet32(0x12345678)));
}

TEST(QuicPlatformTest, QuicEstimateMemoryUsage) {
  quic::QuicString s = "foo";
  // Stubbed out to always return 0.
  EXPECT_EQ(0, quic::QuicEstimateMemoryUsage(s));
}

TEST(QuicPlatformTest, QuicString) {
  quic::QuicString s = "foo";
  EXPECT_EQ('o', s[1]);
}

TEST(QuicPlatformTest, QuicStringPiece) {
  quic::QuicString s = "bar";
  quic::QuicStringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

TEST(QuicPlatformTest, QuicStrCatInts) {
  const int16_t s = -1;
  const uint16_t us = 2;
  const int i = -3;
  const uint32_t ui = 4;
  const int64_t l = -5;
  const uint64_t ul = 6;
  const ptrdiff_t ptrdiff = -7;
  const size_t size = 8;
  const intptr_t intptr = -9;
  const uintptr_t uintptr = 10;
  QuicString answer;
  answer = QuicStrCat(s, us);
  EXPECT_EQ(answer, "-12");
  answer = QuicStrCat(i, ui);
  EXPECT_EQ(answer, "-34");
  answer = QuicStrCat(l, ul);
  EXPECT_EQ(answer, "-56");
  answer = QuicStrCat(ptrdiff, size);
  EXPECT_EQ(answer, "-78");
  answer = QuicStrCat(size, intptr);
  EXPECT_EQ(answer, "8-9");
  answer = QuicStrCat(uintptr, 0);
  EXPECT_EQ(answer, "100");
}

TEST(QuicPlatformTest, QuicStrCatBasics) {
  quic::QuicString result;

  quic::QuicString strs[] = {"Hello", "Cruel", "World"};

  quic::QuicStringPiece pieces[] = {"Hello", "Cruel", "World"};

  const char* c_strs[] = {"Hello", "Cruel", "World"};

  int32_t i32s[] = {'H', 'C', 'W'};
  uint64_t ui64s[] = {12345678910LL, 10987654321LL};

  result = quic::QuicStrCat(false, true, 2, 3);
  EXPECT_EQ(result, "0123");

  result = quic::QuicStrCat(-1);
  EXPECT_EQ(result, "-1");

  result = quic::QuicStrCat(0.5);
  EXPECT_EQ(result, "0.5");

  result = quic::QuicStrCat(strs[1], pieces[2]);
  EXPECT_EQ(result, "CruelWorld");

  result = quic::QuicStrCat(strs[0], ", ", pieces[2]);
  EXPECT_EQ(result, "Hello, World");

  result = quic::QuicStrCat(strs[0], ", ", strs[1], " ", strs[2], "!");
  EXPECT_EQ(result, "Hello, Cruel World!");

  result = quic::QuicStrCat(pieces[0], ", ", pieces[1], " ", pieces[2]);
  EXPECT_EQ(result, "Hello, Cruel World");

  result = quic::QuicStrCat(c_strs[0], ", ", c_strs[1], " ", c_strs[2]);
  EXPECT_EQ(result, "Hello, Cruel World");

  result = quic::QuicStrCat("ASCII ", i32s[0], ", ", i32s[1], " ", i32s[2], "!");
  EXPECT_EQ(result, "ASCII 72, 67 87!");

  result = quic::QuicStrCat(ui64s[0], ", ", ui64s[1], "!");
  EXPECT_EQ(result, "12345678910, 10987654321!");

  quic::QuicString one = "1";
  result = quic::QuicStrCat("And a ", one.size(), " and a ", &result[2] - &result[0],
                      " and a ", one, " 2 3 4", "!");
  EXPECT_EQ(result, "And a 1 and a 2 and a 1 2 3 4!");

  result =
      quic::QuicStrCat("To output a char by ASCII/numeric value, use +: ", '!' + 0);
  EXPECT_EQ(result, "To output a char by ASCII/numeric value, use +: 33");

  float f = 10000.5;
  result = quic::QuicStrCat("Ten K and a half is ", f);
  EXPECT_EQ(result, "Ten K and a half is 10000.5");

  double d = 99999.9;
  result = quic::QuicStrCat("This double number is ", d);
  EXPECT_EQ(result, "This double number is 99999.9");

  result =
      quic::QuicStrCat(1, 22, 333, 4444, 55555, 666666, 7777777, 88888888, 999999999);
  EXPECT_EQ(result, "122333444455555666666777777788888888999999999");
}

TEST(QuicPlatformTest, QuicStrCatMaxArgs) {
  QuicString result;
  // Test 10 up to 26 arguments, the current maximum
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a");
  EXPECT_EQ(result, "123456789a");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b");
  EXPECT_EQ(result, "123456789ab");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c");
  EXPECT_EQ(result, "123456789abc");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d");
  EXPECT_EQ(result, "123456789abcd");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e");
  EXPECT_EQ(result, "123456789abcde");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f");
  EXPECT_EQ(result, "123456789abcdef");
  result =
      quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f", "g");
  EXPECT_EQ(result, "123456789abcdefg");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h");
  EXPECT_EQ(result, "123456789abcdefgh");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i");
  EXPECT_EQ(result, "123456789abcdefghi");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j");
  EXPECT_EQ(result, "123456789abcdefghij");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k");
  EXPECT_EQ(result, "123456789abcdefghijk");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l");
  EXPECT_EQ(result, "123456789abcdefghijkl");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m");
  EXPECT_EQ(result, "123456789abcdefghijklm");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n");
  EXPECT_EQ(result, "123456789abcdefghijklmn");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n", "o");
  EXPECT_EQ(result, "123456789abcdefghijklmno");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n", "o", "p");
  EXPECT_EQ(result, "123456789abcdefghijklmnop");
  result = quic::QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q");
  EXPECT_EQ(result, "123456789abcdefghijklmnopq");
  // No limit thanks to C++11's variadic templates
  result = quic::QuicStrCat(
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, "a", "b", "c", "d", "e", "f", "g", "h",
      "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w",
      "x", "y", "z", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
      "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z");
  EXPECT_EQ(result,
            "12345678910abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
