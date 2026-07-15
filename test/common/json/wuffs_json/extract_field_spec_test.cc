#include "source/common/json/wuffs_json/extract_field_spec.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace Wuffs {
namespace {

// ============================================================================
// parseExtractFieldSpec — valid paths
// ============================================================================

TEST(ParseExtractFieldSpecTest, DepthOneScalar) {
  auto result = parseExtractFieldSpec("model");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 1u);
  EXPECT_EQ(result->segments[0].key, "model");
  EXPECT_FALSE(result->segments[0].is_array_element);
  EXPECT_EQ(result->depth(), 1);
  EXPECT_EQ(result->path, "model");
}

TEST(ParseExtractFieldSpecTest, DepthOneArray) {
  auto result = parseExtractFieldSpec("messages[]");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 2u);
  EXPECT_EQ(result->segments[0].key, "messages");
  EXPECT_FALSE(result->segments[0].is_array_element);
  EXPECT_TRUE(result->segments[1].is_array_element);
  EXPECT_EQ(result->depth(), 2);
}

TEST(ParseExtractFieldSpecTest, DepthTwoScalarInArray) {
  auto result = parseExtractFieldSpec("messages[].role");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 3u);
  EXPECT_EQ(result->segments[0].key, "messages");
  EXPECT_TRUE(result->segments[1].is_array_element);
  EXPECT_EQ(result->segments[2].key, "role");
  EXPECT_EQ(result->depth(), 3);
}

TEST(ParseExtractFieldSpecTest, DepthThreeNestedDicts) {
  auto result = parseExtractFieldSpec("params._meta.traceparent");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 3u);
  EXPECT_EQ(result->segments[0].key, "params");
  EXPECT_EQ(result->segments[1].key, "_meta");
  EXPECT_EQ(result->segments[2].key, "traceparent");
  EXPECT_EQ(result->depth(), 3);
}

TEST(ParseExtractFieldSpecTest, NestedArrays) {
  auto result = parseExtractFieldSpec("messages[].content[]");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 4u);
  EXPECT_EQ(result->segments[0].key, "messages");
  EXPECT_TRUE(result->segments[1].is_array_element);
  EXPECT_EQ(result->segments[2].key, "content");
  EXPECT_TRUE(result->segments[3].is_array_element);
  EXPECT_EQ(result->depth(), 4);
}

TEST(ParseExtractFieldSpecTest, RootArray) {
  // "[].role" is syntactically valid even if unusual in practice.
  auto result = parseExtractFieldSpec("[].role");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 2u);
  EXPECT_TRUE(result->segments[0].is_array_element);
  EXPECT_EQ(result->segments[1].key, "role");
  EXPECT_EQ(result->depth(), 2);
}

TEST(ParseExtractFieldSpecTest, UnderscorePrefixedKey) {
  auto result = parseExtractFieldSpec("_meta");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 1u);
  EXPECT_EQ(result->segments[0].key, "_meta");
}

// ============================================================================
// parseExtractFieldSpec — malformed paths
// ============================================================================

TEST(ParseExtractFieldSpecTest, EmptyPathRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("").ok());
}

TEST(ParseExtractFieldSpecTest, LeadingDotRejected) {
  EXPECT_FALSE(parseExtractFieldSpec(".model").ok());
}

TEST(ParseExtractFieldSpecTest, TrailingDotRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("model.").ok());
}

TEST(ParseExtractFieldSpecTest, DoubleDotRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("params..name").ok());
}

TEST(ParseExtractFieldSpecTest, DotBeforeArrayRejected) {
  // buildPatternPath never produces '.[]' — '[]' always directly follows the parent key.
  EXPECT_FALSE(parseExtractFieldSpec("messages.[]").ok());
}

TEST(ParseExtractFieldSpecTest, UnmatchedOpenBracketRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("messages[").ok());
}

TEST(ParseExtractFieldSpecTest, UnmatchedCloseBracketRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("messages]").ok());
}

TEST(ParseExtractFieldSpecTest, NonEmptySubscriptRejected) {
  // Only '[]' is valid — '[0]', '[key]', '[*]' are not supported.
  EXPECT_FALSE(parseExtractFieldSpec("messages[0]").ok());
  EXPECT_FALSE(parseExtractFieldSpec("messages[key]").ok());
}

TEST(ParseExtractFieldSpecTest, NestedBracketsRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("a[[]]").ok());
}

// Error messages must be non-empty and contain relevant context (not just "false").
TEST(ParseExtractFieldSpecTest, ErrorMessageIsInformative) {
  auto result = parseExtractFieldSpec("messages.[]");
  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.status().message().empty());
}

// ============================================================================
// ExtractFieldSpec::canonicalPath
// ============================================================================

// canonicalPath() must produce the same string that WuffsJsonCursor::buildPatternPath()
// would produce for the same position in the JSON tree.

TEST(CanonicalPathTest, ScalarMatchesBuildPatternPath) {
  auto result = parseExtractFieldSpec("model");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->canonicalPath(), "model");
}

TEST(CanonicalPathTest, NestedScalarMatchesBuildPatternPath) {
  auto result = parseExtractFieldSpec("messages[].role");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->canonicalPath(), "messages[].role");
}

TEST(CanonicalPathTest, ThreeLevelNestedDicts) {
  auto result = parseExtractFieldSpec("params._meta.traceparent");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->canonicalPath(), "params._meta.traceparent");
}

TEST(CanonicalPathTest, ArrayPath) {
  auto result = parseExtractFieldSpec("messages[]");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->canonicalPath(), "messages[]");
}

TEST(CanonicalPathTest, NestedArrays) {
  auto result = parseExtractFieldSpec("messages[].content[]");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->canonicalPath(), "messages[].content[]");
}

// Canonical path is round-trippable: parse → canonicalPath == original path.
TEST(CanonicalPathTest, RoundTrip) {
  const std::vector<std::string> paths = {
      "model",
      "messages[]",
      "messages[].role",
      "params._meta.traceparent",
      "messages[].content[]",
      "_meta",
      "[].role",
  };
  for (const auto& p : paths) {
    auto result = parseExtractFieldSpec(p);
    ASSERT_TRUE(result.ok()) << "failed to parse: " << p;
    EXPECT_EQ(result->canonicalPath(), p) << "round-trip failed for: " << p;
  }
}

// ============================================================================
// DecoderConfig::requiredMaxDepth
// ============================================================================

TEST(DecoderConfigTest, EmptySpecListReturnsZero) {
  DecoderConfig cfg;
  EXPECT_EQ(cfg.requiredMaxDepth(), 0);
}

TEST(DecoderConfigTest, SingleSpecDepth) {
  DecoderConfig cfg;
  auto spec = parseExtractFieldSpec("model");
  ASSERT_TRUE(spec.ok());
  cfg.extract_fields.push_back(std::move(*spec));
  EXPECT_EQ(cfg.requiredMaxDepth(), 1);
}

TEST(DecoderConfigTest, MultipleSpecsReturnMax) {
  DecoderConfig cfg;
  for (const auto* path : {"model", "messages[].role", "params._meta.traceparent"}) {
    auto s = parseExtractFieldSpec(path);
    ASSERT_TRUE(s.ok());
    cfg.extract_fields.push_back(std::move(*s));
  }
  EXPECT_EQ(cfg.requiredMaxDepth(), 3);
}

TEST(DecoderConfigTest, DeepestSpecDrivesMaxDepth) {
  DecoderConfig cfg;
  for (const auto* path : {"messages[].content[]", "model"}) {
    auto s = parseExtractFieldSpec(path);
    ASSERT_TRUE(s.ok());
    cfg.extract_fields.push_back(std::move(*s));
  }
  EXPECT_EQ(cfg.requiredMaxDepth(), 4);
}

} // namespace
} // namespace Wuffs
} // namespace Json
} // namespace Envoy
