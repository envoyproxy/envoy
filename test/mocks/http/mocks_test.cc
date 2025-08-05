#include "envoy/http/header_map.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
using ::testing::_;
using ::testing::Not;

namespace Http {

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

TEST(ContainsHeaderTest, ConstHeaderMap) {
  const Http::TestRequestHeaderMapImpl header_map{{"key", "expected value"}};

  // Positive checks.
  EXPECT_THAT(header_map, ContainsHeader("key", "expected value"));
  EXPECT_THAT(header_map, ContainsHeader("key", _));

  // Negative checks.
  EXPECT_THAT(header_map, Not(ContainsHeader("key", "other value")));
  EXPECT_THAT(header_map, Not(ContainsHeader("other key", _)));
}

TEST(ContainsHeaderTest, MutableHeaderMap) {
  Http::TestRequestHeaderMapImpl header_map;

  // Negative checks.
  EXPECT_THAT(header_map, Not(ContainsHeader("key", "other value")));
  EXPECT_THAT(header_map, Not(ContainsHeader("other key", _)));

  header_map.addCopy("key", "expected value");

  // Positive checks.
  EXPECT_THAT(header_map, ContainsHeader("key", "expected value"));
  EXPECT_THAT(header_map, ContainsHeader("key", _));
}

TEST(ContainsHeaderTest, LowerCaseStringArguments) {
  Http::TestRequestHeaderMapImpl header_map;
  Http::LowerCaseString key("key");
  Http::LowerCaseString other_key("other_key");

  // Negative checks.
  EXPECT_THAT(header_map, Not(ContainsHeader(key, "other value")));
  EXPECT_THAT(header_map, Not(ContainsHeader(other_key, _)));

  header_map.addCopy(key, "expected value");
  header_map.addCopy(other_key, "ValUe");

  // Positive checks.
  EXPECT_THAT(header_map, ContainsHeader(key, "expected value"));
  EXPECT_THAT(header_map, ContainsHeader(other_key, _));
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
