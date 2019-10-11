#include "common/http/http1/header_formatter.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http1 {
TEST(ProperCaseHeaderKeyFormatterTest, Formatting) {
  ProperCaseHeaderKeyFormatter formatter;

  const std::string downcased = "content-type";
  const std::string specialCharacters = "a!d#sa-lo";
  EXPECT_EQ(formatter.format(downcased), "Content-Type");
  EXPECT_EQ(formatter.format(specialCharacters), "A!D#Sa-Lo");
}
} // namespace Http1
} // namespace Http
} // namespace Envoy