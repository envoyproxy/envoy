#include <type_traits>

#include "source/common/http/character_set_validation.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(CharacterSetValidationTest, RawInitializationWorksCorrectly) {
  // This table has the following characters "enabled"
  // every 8th character in a 32 character row
  // and every row is shifted by 1
  constexpr CharTable kCharTable = {{
      // control characters
      0b10000000100000001000000010000000,
      // !"#$%&'()*+,-./0123456789:;<=>?
      0b01000000010000000100000001000000,
      //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
      0b00100000001000000010000000100000,
      //`abcdefghijklmnopqrstuvwxyz{|}~
      0b00010000000100000001000000010000,
      // extended ascii
      0b00001000000010000000100000001000,
      0b00000100000001000000010000000100,
      0b00000010000000100000001000000010,
      0b00000001000000010000000100000001,
  }};

  for (unsigned c = 0; c < 256; ++c) {
    bool result = kCharTable.hasChar(c);
    // c / 32 is the shift value of every row
    // ((c % 32) % 8) is the 8th character in that row
    ASSERT_EQ(result, ((c % 32) % 8) == c / 32) << c;
  }
}

TEST(CharacterSetValidationTest, AlphanumericsAndCharsInitializesCorrectly) {
  constexpr CharTable kCharTable = CharTables::kAlphanumeric | CharTable::fromChars("!");

  for (unsigned c = 0; c < 256; ++c) {
    bool result = kCharTable.hasChar(c);
    bool expected =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '!';
    EXPECT_EQ(result, expected) << c;
  }
}

TEST(CharacterSetValidationTest, NotOperatorInitializesCorrectly) {
  constexpr CharTable kCharTable = ~(CharTables::kAlphanumeric | CharTable::fromChars("!"));

  for (unsigned c = 0; c < 256; ++c) {
    bool result = kCharTable.hasChar(c);
    bool expected =
        !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '!');
    EXPECT_EQ(result, expected) << c;
  }
}

TEST(CharacterSetValidationTest, AndNotOperatorInitializesCorrectly) {
  constexpr CharTable kCharTable = CharTables::kAlphanumeric & ~CharTables::kUppercase;

  for (unsigned c = 0; c < 256; ++c) {
    bool result = kCharTable.hasChar(c);
    bool expected = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    EXPECT_EQ(result, expected) << c;
  }
}

TEST(CharacterSetValidationTest, FromCharsInitializesCorrectly) {
  constexpr CharTable kCharTable = CharTable::fromChars("!");

  for (unsigned c = 0; c < 256; ++c) {
    bool result = kCharTable.hasChar(c);
    bool expected = (c == '!');
    EXPECT_EQ(result, expected) << c;
  }
}

// Primary template - selected if expression CAN be constexpr
template <typename Lambda>
constexpr auto is_constexpr_impl(Lambda lambda, int)
    -> decltype((void)std::integral_constant<bool, (lambda(), true)>::value, bool{}) {
  return true;
}

// Fallback - selected if expression CANNOT be constexpr
template <typename Lambda> constexpr bool is_constexpr_impl(Lambda, long) { return false; }

TEST(CharacterSetValidationTest, WorksFromDynamicData) {
  // Code coverage says none of the functions are covered if we don't test using them
  // with dynamic data, since all constexpr usage is compiled-out!
  // absl::StrCat doesn't have a constexpr variant so this forces non-constexpr usage.
  static_assert(
      !is_constexpr_impl([]() { return absl::StrCat("!"); }, 0),
      "Oh no, StrCat can be constexpr-evaluated - replace StrCat with something that can't!");
  const CharTable kCharTable =
      (CharTable::fromChars(absl::StrCat("!")) | CharTable::fromChars(absl::StrCat("@$"))) &
      ~CharTable::fromChars(absl::StrCat("$"));

  for (unsigned c = 0; c < 256; ++c) {
    bool result = kCharTable.hasChar(c);
    bool expected = (c == '!' || c == '@');
    EXPECT_EQ(result, expected) << c;
  }
}

} // namespace Http
} // namespace Envoy
