#include <array>
#include <cstdint>
#include <string>

#include "envoy/http/metadata_interface.h"

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"

#include "test/test_common/logging.h"

#include "fmt/ranges.h" // This makes spdlog call the formatter<Envoy::Http::MetadataMap> when logging.
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::HasSubstr;

namespace Envoy {
namespace Http {
namespace {

TEST(MetadataMapTest, KeyValueEscaped) {
  MetadataMap m;
  m.insert({"a1", "a2"});
  m.insert({"b1", "b2"});
  // Broken at the 2nd byte.
  m.insert({"non-utf8", "\xc3\x28"});
  // Length incorrect: the (0xf7) "1110" requires 4 bytes, but there are only 2.
  // ASAN should fail if no escaping.
  m.insert({"broken-utf8", "\xf7\x28"});

  // Now check the "<<" operator.
  std::ostringstream oss;
  oss << m;
  std::string output_str = oss.str();

  EXPECT_THAT(output_str, HasSubstr("key: b1, value: b2"));
  EXPECT_THAT(output_str, HasSubstr("key: a1, value: a2"));
  // Escaped.
  EXPECT_THAT(output_str, HasSubstr("key: non-utf8, value: \\303("));
  EXPECT_THAT(output_str, HasSubstr("key: broken-utf8, value: \\367("));

  // The spdlog macro expansion should just work.
  EXPECT_LOG_CONTAINS("error", "key: non-utf8, value: \\303(",
                      ENVOY_LOG_MISC(error, "output: {}", m));
  EXPECT_LOG_CONTAINS("error", "key: broken-utf8, value: \\367(",
                      ENVOY_LOG_MISC(error, "output: {}", m));
}

} // namespace

} // namespace Http
} // namespace Envoy
