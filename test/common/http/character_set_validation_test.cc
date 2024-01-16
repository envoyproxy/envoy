#include "source/common/http/character_set_validation.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(CharacterSetValidationTest, Test) {
  // This table has the following characters "enabled"
  // every 8th character in a 32 character row
  // and every row is shifted by 1
  constexpr std::array<uint32_t, 8> kCharTable = {
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
  };

  for (unsigned c = 0; c < 256; ++c) {
    bool result = testCharInTable(kCharTable, static_cast<unsigned char>(c));
    // c / 32 is the shift value of every row
    // ((c % 32) % 8) is the 8th character in that row
    ASSERT_EQ(result, ((c % 32) % 8) == c / 32);
  }
}

} // namespace Http
} // namespace Envoy
