#include <string>

#include "source/extensions/filters/http/cache_v2/etag_utils.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace EtagUtils {
namespace {

struct ComparisonTestCase {
  absl::string_view lhs_;
  absl::string_view rhs_;
  bool strong_match_;
  bool weak_match_;
};

class EtagComparisonTest : public testing::TestWithParam<ComparisonTestCase> {};

TEST_P(EtagComparisonTest, MatchesAsExpected) {
  const ComparisonTestCase& test_case = GetParam();
  EXPECT_EQ(test_case.strong_match_, strongMatch(test_case.lhs_, test_case.rhs_));
  EXPECT_EQ(test_case.weak_match_, weakMatch(test_case.lhs_, test_case.rhs_));
}

INSTANTIATE_TEST_SUITE_P(Rfc9110Examples, EtagComparisonTest,
                         testing::Values(ComparisonTestCase{R"(W/"1")", R"(W/"1")", false, true},
                                         ComparisonTestCase{R"(W/"1")", R"(W/"2")", false, false},
                                         ComparisonTestCase{R"(W/"1")", R"("1")", false, true},
                                         ComparisonTestCase{R"("1")", R"("1")", true, true}));

TEST(EtagUtilsTest, EmptyOpaqueTagsMatch) {
  EXPECT_TRUE(strongMatch(R"("")", R"("")"));
  EXPECT_TRUE(weakMatch(R"(W/"")", R"("")"));
}

TEST(EtagUtilsTest, OpaqueTagsAreCaseSensitive) {
  EXPECT_FALSE(strongMatch(R"("etag")", R"("ETag")"));
  EXPECT_FALSE(weakMatch(R"(W/"etag")", R"("ETag")"));
}

TEST(EtagUtilsTest, AllowsEveryAsciiEtagCharacter) {
  constexpr absl::string_view etag =
      R"("!#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~")";
  EXPECT_TRUE(strongMatch(etag, etag));
}

TEST(EtagUtilsTest, AllowsObsText) {
  const std::string etag =
      std::string{"\""} + static_cast<char>(0x80) + static_cast<char>(0xff) + "\"";
  EXPECT_TRUE(strongMatch(etag, etag));
}

class InvalidEtagTest : public testing::TestWithParam<absl::string_view> {};

TEST_P(InvalidEtagTest, DoesNotMatch) {
  EXPECT_FALSE(strongMatch(GetParam(), GetParam()));
  EXPECT_FALSE(weakMatch(GetParam(), GetParam()));
  EXPECT_FALSE(weakMatch(GetParam(), R"("valid")"));
  EXPECT_FALSE(weakMatch(R"("valid")", GetParam()));
}

INSTANTIATE_TEST_SUITE_P(InvalidEntityTags, InvalidEtagTest,
                         testing::Values("", "etag", R"("unterminated)", R"(unterminated")",
                                         R"(w/"etag")", R"(W\"etag\")", R"("embedded"quote")",
                                         "\"space \"", "\"tab\t\"", "\"delete\x7f\"",
                                         R"("etag"suffix)", R"(W/W/"etag")"));

TEST(IfNoneMatchTest, MatchesWildcard) { EXPECT_TRUE(ifNoneMatch("*", "")); }

TEST(IfNoneMatchTest, WeaklyMatchesAnyEntityTagInList) {
  EXPECT_TRUE(ifNoneMatch(R"("one,with-comma", W/"selected", "three")", R"("selected")"));
}

TEST(IfNoneMatchTest, AllowsOptionalWhitespaceAndEmptyListElements) {
  EXPECT_TRUE(ifNoneMatch(" , \t, W/\"selected\" , ", R"("selected")"));
}

TEST(IfNoneMatchTest, DoesNotMatchDifferentEntityTags) {
  EXPECT_FALSE(ifNoneMatch(R"("one", W/"two")", R"("three")"));
}

TEST(IfNoneMatchTest, InvalidFieldValueDoesNotMatch) {
  EXPECT_FALSE(ifNoneMatch(R"("selected" trailing)", R"("selected")"));
  EXPECT_FALSE(ifNoneMatch(R"(*, "selected")", R"("selected")"));
  EXPECT_FALSE(ifNoneMatch(",,,", R"("selected")"));
}

TEST(IfNoneMatchTest, InvalidSelectedEtagDoesNotMatchList) {
  EXPECT_FALSE(ifNoneMatch(R"("selected")", "selected"));
}

} // namespace
} // namespace EtagUtils
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
