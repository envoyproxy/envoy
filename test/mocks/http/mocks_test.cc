#include "envoy/http/header_map.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
using ::testing::Not;

TEST(HttpHeaderMapMatcherTest, MutableValueRef) {
  Http::TestHeaderMapImpl header_map;

  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "value")));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef("other key", "value")));

  header_map.addCopy("key", "value");

  EXPECT_THAT(header_map, HeaderHasValueRef("key", "value"));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "wrong value")));
}

TEST(HeaderHasValueRefTest, ConstValueRef) {
  const Http::TestHeaderMapImpl header_map{{"key", "expected value"}};

  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "other value")));
  EXPECT_THAT(header_map, HeaderHasValueRef("key", "expected value"));
}

TEST(HeaderHasValueRefTest, LowerCaseStringArguments) {
  Http::LowerCaseString key("key"), other_key("other key");
  Http::TestHeaderMapImpl header_map;

  EXPECT_THAT(header_map, Not(HeaderHasValueRef(key, "value")));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef(other_key, "value")));

  header_map.addCopy(key, "value");

  EXPECT_THAT(header_map, HeaderHasValueRef(key, "value"));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef(other_key, "wrong value")));
}
} // namespace Envoy
