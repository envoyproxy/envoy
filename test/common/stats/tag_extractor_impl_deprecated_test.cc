#include "test/common/stats/tag_extractor_test_common.h"

namespace Envoy {
namespace Stats {

TEST(TagExtractorTest, DeprecatedTagExtractors) {
  DefaultTagRegexTester regex_tester;
  regex_tester.testRegex("thrift.thrift_prefix.response", "thrift.thrift_prefix.response", {});
}

} // namespace Stats
} // namespace Envoy
