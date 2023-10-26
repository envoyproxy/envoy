#include "envoy/http/header_map.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
using ::testing::_;
using ::testing::Not;

namespace Http {
TEST(HeaderValueOfTest, ConstHeaderMap) {
  const TestRequestHeaderMapImpl header_map{{"key", "expected value"}};

  // Positive checks.
  EXPECT_THAT(header_map, HeaderValueOf("key", "expected value"));
  EXPECT_THAT(header_map, HeaderValueOf("key", _));

  // Negative checks.
  EXPECT_THAT(header_map, Not(HeaderValueOf("key", "other value")));
  EXPECT_THAT(header_map, Not(HeaderValueOf("other key", _)));
}

TEST(HeaderValueOfTest, MutableHeaderMap) {
  TestRequestHeaderMapImpl header_map;

  // Negative checks.
  EXPECT_THAT(header_map, Not(HeaderValueOf("key", "other value")));
  EXPECT_THAT(header_map, Not(HeaderValueOf("other key", _)));

  header_map.addCopy("key", "expected value");

  // Positive checks.
  EXPECT_THAT(header_map, HeaderValueOf("key", "expected value"));
  EXPECT_THAT(header_map, HeaderValueOf("key", _));
}

TEST(HeaderValueOfTest, LowerCaseString) {
  TestRequestHeaderMapImpl header_map;
  LowerCaseString key("key");
  LowerCaseString other_key("other_key");

  // Negative checks.
  EXPECT_THAT(header_map, Not(HeaderValueOf(key, "other value")));
  EXPECT_THAT(header_map, Not(HeaderValueOf(other_key, _)));

  header_map.addCopy(key, "expected value");
  header_map.addCopy(other_key, "ValUe");

  // Positive checks.
  EXPECT_THAT(header_map, HeaderValueOf(key, "expected value"));
  EXPECT_THAT(header_map, HeaderValueOf(other_key, _));
}

TEST(HttpStatusIsTest, CheckStatus) {
  TestResponseHeaderMapImpl header_map;
  const auto status_matcher = HttpStatusIs(200);

  EXPECT_THAT(header_map, Not(status_matcher));

  header_map.addCopy(Headers::get().Status, "200");

  EXPECT_THAT(header_map, status_matcher);
}

TEST(IsSubsetOfHeadersTest, ConstHeaderMap) {
  const TestRequestHeaderMapImpl header_map{{"first key", "1"}};

  EXPECT_THAT(header_map, IsSubsetOfHeaders(TestRequestHeaderMapImpl{{"first key", "1"}}));
  EXPECT_THAT(header_map,
              IsSubsetOfHeaders(TestRequestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));

  EXPECT_THAT(header_map, Not(IsSubsetOfHeaders(TestRequestHeaderMapImpl{{"third key", "1"}})));
}

TEST(IsSubsetOfHeadersTest, MutableHeaderMap) {
  TestRequestHeaderMapImpl header_map;
  header_map.addCopy("first key", "1");

  EXPECT_THAT(header_map, IsSubsetOfHeaders(TestRequestHeaderMapImpl{{"first key", "1"}}));
  EXPECT_THAT(header_map,
              IsSubsetOfHeaders(TestRequestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));

  EXPECT_THAT(header_map, Not(IsSubsetOfHeaders(TestRequestHeaderMapImpl{{"third key", "1"}})));
}

TEST(IsSupersetOfHeadersTest, ConstHeaderMap) {
  const TestRequestHeaderMapImpl header_map{{"first key", "1"}, {"second key", "2"}};

  EXPECT_THAT(header_map, IsSupersetOfHeaders(
                              TestRequestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));
  EXPECT_THAT(header_map, IsSupersetOfHeaders(TestRequestHeaderMapImpl{{"first key", "1"}}));

  EXPECT_THAT(header_map, Not(IsSupersetOfHeaders(TestRequestHeaderMapImpl{{"third key", "1"}})));
}

TEST(IsSupersetOfHeadersTest, MutableHeaderMap) {
  TestRequestHeaderMapImpl header_map;
  header_map.addCopy("first key", "1");
  header_map.addCopy("second key", "2");

  EXPECT_THAT(header_map, IsSupersetOfHeaders(
                              TestRequestHeaderMapImpl{{"first key", "1"}, {"second key", "2"}}));
  EXPECT_THAT(header_map, IsSupersetOfHeaders(TestRequestHeaderMapImpl{{"first key", "1"}}));

  EXPECT_THAT(header_map, Not(IsSupersetOfHeaders(TestRequestHeaderMapImpl{{"third key", "1"}})));
}
} // namespace Http

TEST(HeaderHasValueRefTest, MutableValueRef) {
  Http::TestRequestHeaderMapImpl header_map;

  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "value")));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef("other key", "value")));

  header_map.addCopy("key", "value");

  EXPECT_THAT(header_map, HeaderHasValueRef("key", "value"));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "wrong value")));
}

TEST(HeaderHasValueRefTest, ConstValueRef) {
  const Http::TestRequestHeaderMapImpl header_map{{"key", "expected value"}};

  EXPECT_THAT(header_map, Not(HeaderHasValueRef("key", "other value")));
  EXPECT_THAT(header_map, HeaderHasValueRef("key", "expected value"));
}

TEST(HeaderHasValueRefTest, LowerCaseStringArguments) {
  Http::LowerCaseString key("key"), other_key("other key");
  Http::TestRequestHeaderMapImpl header_map;

  EXPECT_THAT(header_map, Not(HeaderHasValueRef(key, "value")));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef(other_key, "value")));

  header_map.addCopy(key, "value");

  EXPECT_THAT(header_map, HeaderHasValueRef(key, "value"));
  EXPECT_THAT(header_map, Not(HeaderHasValueRef(other_key, "wrong value")));
}

TEST(HeaderMatcherTest, OutputsActualHeadersOnMatchFailure) {
  Http::TestRequestHeaderMapImpl header_map{{"test-header", "value"}};
  Http::TestRequestHeaderMapImpl expected_header_map{{"test-header2", "value2"}};
  {
    // Check that actual headers are in the output if the match fails.
    testing::StringMatchResultListener output;
    EXPECT_FALSE(::testing::ExplainMatchResult(IsSupersetOfHeaders(expected_header_map), header_map,
                                               &output));
    EXPECT_THAT(output.str(), testing::HasSubstr(R"(
Actual headers:
'test-header', 'value')"));
  }
  {
    // Check that actual headers are not output if the match succeeds.
    testing::StringMatchResultListener output;
    EXPECT_TRUE(
        ::testing::ExplainMatchResult(IsSupersetOfHeaders(header_map), header_map, &output));
    EXPECT_THAT(output.str(), testing::Not(testing::HasSubstr("Actual headers")));
  }
}

} // namespace Envoy
