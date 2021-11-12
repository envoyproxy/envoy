#include "source/common/http/http1/header_formatter.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http1 {
TEST(ProperCaseHeaderKeyFormatterTest, Formatting) {
  ProperCaseHeaderKeyFormatter formatter;

  const std::string downcased = "content-type";
  EXPECT_EQ(formatter.format(downcased), "Content-Type");

  const std::string special_characters = "a!d#sa-lo";
  EXPECT_EQ(formatter.format(special_characters), "A!D#Sa-Lo");

  const std::string empty;
  EXPECT_EQ(formatter.format(empty), "");

  const std::string single_character = "a";
  EXPECT_EQ(formatter.format(single_character), "A");
}
} // namespace Http1
} // namespace Http
} // namespace Envoy
