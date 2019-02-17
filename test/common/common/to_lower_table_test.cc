#include "common/common/to_lower_table.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(ToLowerTableTest, All) {
  ToLowerTable table;

  {
    std::string input("hello");
    table.toLowerCase(input);
    EXPECT_EQ(input, "hello");
  }
  {
    std::string input("HellO");
    table.toLowerCase(input);
    EXPECT_EQ(input, "hello");
  }
  {
    std::string input("123HELLO456");
    table.toLowerCase(input);
    EXPECT_EQ(input, "123hello456");
  }
  {
    std::string input("\x90HELLO\x90");
    table.toLowerCase(input);
    EXPECT_EQ(input, "\x90hello\x90");
  }
}
} // namespace Envoy
