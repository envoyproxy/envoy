#include <tuple>
#include <utility>
#include <vector>

#include "source/common/http/path_utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class PathUtilityTest : public testing::Test {
public:
  // This is an indirect way to build a header entry for
  // PathUtil::canonicalPath(), since we don't have direct access to the
  // HeaderMapImpl constructor.
  const HeaderEntry& pathHeaderEntry(const std::string& path_value) {
    headers_.setPath(path_value);
    return *headers_.Path();
  }
  const HeaderEntry& hostHeaderEntry(const std::string& host_value) {
    headers_.setHost(host_value);
    return *headers_.Host();
  }
  TestRequestHeaderMapImpl headers_;
};

// Already normalized path don't change.
TEST_F(PathUtilityTest, AlreadyNormalPaths) {
  const std::vector<std::string> normal_paths{"/xyz", "/x/y/z"};
  for (const auto& path : normal_paths) {
    auto& path_header = pathHeaderEntry(path);
    const auto result = PathUtil::canonicalPath(headers_);
    EXPECT_TRUE(result) << "original path: " << path;
    EXPECT_EQ(path_header.value().getStringView(), absl::string_view(path));
  }
}

// Already normalized path don't change.
TEST(PathTransformationTest, AlreadyNormalPaths) {
  const std::vector<std::string> normal_paths{"/xyz", "/x/y/z"};
  for (const auto& path : normal_paths) {
    const auto result = PathTransformer::rfcNormalize(absl::string_view(path));
    EXPECT_EQ(path, result);
  }
}

// Invalid paths are rejected.
TEST_F(PathUtilityTest, InvalidPaths) {
  const std::vector<std::string> invalid_paths{"/xyz/.%00../abc", "/xyz/%00.%00./abc",
                                               "/xyz/AAAAA%%0000/abc"};
  for (const auto& path : invalid_paths) {
    pathHeaderEntry(path);
    EXPECT_FALSE(PathUtil::canonicalPath(headers_)) << "original path: " << path;
  }
}

// Invalid paths are rejected.
TEST(PathTransformationTest, InvalidPaths) {
  const std::vector<std::string> invalid_paths{"/xyz/.%00../abc", "/xyz/%00.%00./abc",
                                               "/xyz/AAAAA%%0000/abc"};
  for (const auto& path : invalid_paths) {
    EXPECT_FALSE(PathTransformer::rfcNormalize(path).has_value()) << "original path: " << path;
  }
}

// Paths that are valid get normalized.
TEST_F(PathUtilityTest, NormalizeValidPaths) {
  const std::vector<std::pair<std::string, std::string>> non_normal_pairs{
      {"/a/b/../c", "/a/c"},        // parent dir
      {"/a/b/./c", "/a/b/c"},       // current dir
      {"a/b/../c", "/a/c"},         // non / start
      {"/a/b/../../../../c", "/c"}, // out number parent
      {"/a/..\\c", "/c"},           // "..\\" canonicalization
      {"/%c0%af", "/%c0%af"},       // 2 bytes unicode reserved characters
      {"/%5c%25", "/%5c%25"},       // reserved characters
      {"/a/b/%2E%2E/c", "/a/c"},    // %2E escape
      {"/a/b/%2E./c", "/a/c"},      {"/a/b/%2E/c", "/a/b/c"}, {"..", "/"},
      {"/a/b/%2E/c", "/a/b/c"},     {"/../a/b", "/a/b"},      {"/./a/b", "/a/b"},
  };

  for (const auto& path_pair : non_normal_pairs) {
    auto& path_header = pathHeaderEntry(path_pair.first);
    const auto result = PathUtil::canonicalPath(headers_);
    EXPECT_TRUE(result) << "original path: " << path_pair.first;
    EXPECT_EQ(path_header.value().getStringView(), path_pair.second)
        << "original path: " << path_pair.second;
  }
}

// Already normalized path don't change.
TEST(PathTransformationTest, NormalizeValidPaths) {

  const std::vector<std::pair<std::string, std::string>> non_normal_pairs{
      {"/a/b/../c", "/a/c"},        // parent dir
      {"/a/b/./c", "/a/b/c"},       // current dir
      {"a/b/../c", "/a/c"},         // non / start
      {"/a/b/../../../../c", "/c"}, // out number parent
      {"/a/..\\c", "/c"},           // "..\\" canonicalization
      {"/%c0%af", "/%c0%af"},       // 2 bytes unicode reserved characters
      {"/%5c%25", "/%5c%25"},       // reserved characters
      {"/a/b/%2E%2E/c", "/a/c"}     // %2E escape
  };

  const std::vector<std::string> normal_paths{"/xyz", "/x/y/z"};
  for (const auto& path : normal_paths) {
    const auto result = PathTransformer::rfcNormalize(absl::string_view(path)).value();
    EXPECT_EQ(path, result);
  }
  for (const auto& path_pair : non_normal_pairs) {
    const auto& path = path_pair.first;
    const auto result = PathTransformer::rfcNormalize(absl::string_view(path)).value();
    EXPECT_EQ(result, path_pair.second);
  }
}

// Paths that are valid get normalized.
TEST_F(PathUtilityTest, NormalizeCasePath) {
  const std::vector<std::pair<std::string, std::string>> non_normal_pairs{
      {"/A/B/C", "/A/B/C"},           // not normalize to lower case
      {"/a/b/%2E%2E/c", "/a/c"},      // %2E can be normalized to .
      {"/a/b/%2e%2e/c", "/a/c"},      // %2e can be normalized to .
      {"/a/%2F%2f/c", "/a/%2F%2f/c"}, // %2F is not normalized to %2f
  };

  for (const auto& path_pair : non_normal_pairs) {
    auto& path_header = pathHeaderEntry(path_pair.first);
    const auto result = PathUtil::canonicalPath(headers_);
    EXPECT_TRUE(result) << "original path: " << path_pair.first;
    EXPECT_EQ(path_header.value().getStringView(), path_pair.second)
        << "original path: " << path_pair.second;
  }
}

// Paths that are valid get normalized.
TEST(PathTransformationTest, NormalizeCasePath) {
  const std::vector<std::pair<std::string, std::string>> non_normal_pairs{
      {"/A/B/C", "/A/B/C"},           // not normalize to lower case
      {"/a/b/%2E%2E/c", "/a/c"},      // %2E can be normalized to .
      {"/a/b/%2e%2e/c", "/a/c"},      // %2e can be normalized to .
      {"/a/%2F%2f/c", "/a/%2F%2f/c"}, // %2F is not normalized to %2f
  };

  for (const auto& path_pair : non_normal_pairs) {
    const auto& path = path_pair.first;
    const auto result = PathTransformer::rfcNormalize(absl::string_view(path)).value();
    EXPECT_EQ(result, path_pair.second);
  }
}

// These test cases are explicitly not covered above:
// "/../c\r\n\"  '\n' '\r' should be excluded by http parser
// "/a/\0c",     '\0' should be excluded by http parser

// Paths that are valid get normalized.
TEST_F(PathUtilityTest, MergeSlashes) {
  auto mergeSlashes = [this](const std::string& path_value) {
    auto& path_header = pathHeaderEntry(path_value);
    PathUtil::mergeSlashes(headers_);
    auto sanitized_path_value = path_header.value().getStringView();
    return std::string(sanitized_path_value);
  };
  EXPECT_EQ("", mergeSlashes(""));                        // empty
  EXPECT_EQ("a/b/c", mergeSlashes("a//b/c"));             // relative
  EXPECT_EQ("/a/b/c/", mergeSlashes("/a//b/c/"));         // ends with slash
  EXPECT_EQ("a/b/c/", mergeSlashes("a//b/c/"));           // relative ends with slash
  EXPECT_EQ("/a", mergeSlashes("/a"));                    // no-op
  EXPECT_EQ("/a/b/c", mergeSlashes("//a/b/c"));           // double / start
  EXPECT_EQ("/a/b/c", mergeSlashes("/a//b/c"));           // double / in the middle
  EXPECT_EQ("/a/b/c/", mergeSlashes("/a/b/c//"));         // double / end
  EXPECT_EQ("/a/b/c", mergeSlashes("/a///b/c"));          // triple / in the middle
  EXPECT_EQ("/a/b/c", mergeSlashes("/a////b/c"));         // quadruple / in the middle
  EXPECT_EQ("/a/b?a=///c", mergeSlashes("/a//b?a=///c")); // slashes in the query are ignored
  EXPECT_EQ("/a/b?", mergeSlashes("/a//b?"));             // empty query
  EXPECT_EQ("/a/?b", mergeSlashes("//a/?b"));             // ends with slash + query
}

TEST(PathTransformationTest, MergeSlashes) {
  EXPECT_EQ("", PathTransformer::mergeSlashes("").value());                // empty
  EXPECT_EQ("a/b/c", PathTransformer::mergeSlashes("a//b/c").value());     // relative
  EXPECT_EQ("/a/b/c/", PathTransformer::mergeSlashes("/a//b/c/").value()); // ends with slash
  EXPECT_EQ("a/b/c/", PathTransformer::mergeSlashes("a//b/c/").value()); // relative ends with slash
  EXPECT_EQ("/a", PathTransformer::mergeSlashes("/a").value());          // no-op
  EXPECT_EQ("/a/b/c", PathTransformer::mergeSlashes("//a/b/c").value()); // double / start
  EXPECT_EQ("/a/b/c", PathTransformer::mergeSlashes("/a//b/c").value()); // double / in the middle
  EXPECT_EQ("/a/b/c/", PathTransformer::mergeSlashes("/a/b/c//").value()); // double / end
  EXPECT_EQ("/a/b/c", PathTransformer::mergeSlashes("/a///b/c").value());  // triple / in the middle
  EXPECT_EQ("/a/b/c",
            PathTransformer::mergeSlashes("/a////b/c").value()); // quadruple / in the middle
  EXPECT_EQ(
      "/a/b?a=///c",
      PathTransformer::mergeSlashes("/a//b?a=///c").value()); // slashes in the query are ignored
  EXPECT_EQ("/a/b?", PathTransformer::mergeSlashes("/a//b?").value()); // empty query
  EXPECT_EQ("/a/?b", PathTransformer::mergeSlashes("//a/?b").value()); // ends with slash + query
}

TEST_F(PathUtilityTest, RemoveQueryAndFragment) {
  EXPECT_EQ("", PathUtil::removeQueryAndFragment(""));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?param=value"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?param=value1&param=value2"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc??"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc??param=value"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc#"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc#fragment"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc#fragment?param=value"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc##"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc#?"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc#?param=value"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?#"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?#fragment"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?param=value#"));
  EXPECT_EQ("/abc", PathUtil::removeQueryAndFragment("/abc?param=value#fragment"));
}

class PathTransformerTest : public testing::Test {
public:
  void setPathTransformer(const std::string& path_transformation_config) {
    envoy::type::http::v3::PathTransformation path_transformer;
    TestUtility::loadFromYaml(path_transformation_config, path_transformer);

    path_transformer_ = std::make_unique<PathTransformer>(path_transformer);
  }

  const PathTransformer& pathTransformer() { return *path_transformer_; }

  std::unique_ptr<PathTransformer> path_transformer_;
  NormalizePathAction action_{NormalizePathAction::Continue};
};

TEST_F(PathTransformerTest, MergeSlashes) {
  std::string path_transformation_config = R"EOF(
      operations:
      - merge_slashes: {}
)EOF";
  setPathTransformer(path_transformation_config);
  PathTransformer const& path_transformer = pathTransformer();
  EXPECT_EQ("", path_transformer.transform("", action_).value());                // empty
  EXPECT_EQ("a/b/c", path_transformer.transform("a//b/c", action_).value());     // relative
  EXPECT_EQ("/a/b/c/", path_transformer.transform("/a//b/c/", action_).value()); // ends with slash
  EXPECT_EQ("a/b/c/",
            path_transformer.transform("a//b/c/", action_).value());  // relative ends with slash
  EXPECT_EQ("/a", path_transformer.transform("/a", action_).value()); // no-op
  EXPECT_EQ("/a/b/c", path_transformer.transform("//a/b/c", action_).value()); // double / start
  EXPECT_EQ("/a/b/c",
            path_transformer.transform("/a//b/c", action_).value()); // double / in the middle
  EXPECT_EQ("/a/b/c/", path_transformer.transform("/a/b/c//", action_).value()); // double / end
  EXPECT_EQ("/a/b/c",
            path_transformer.transform("/a///b/c", action_).value()); // triple / in the middle
  EXPECT_EQ("/a/b/c",
            path_transformer.transform("/a////b/c", action_).value()); // quadruple / in the middle
  EXPECT_EQ("/a/b?a=///c", path_transformer.transform("/a//b?a=///c", action_)
                               .value()); // slashes in the query are ignored
  EXPECT_EQ("/a/b?", path_transformer.transform("/a//b?", action_).value()); // empty query
  EXPECT_EQ("/a/?b",
            path_transformer.transform("//a/?b", action_).value()); // ends with slash + query
}

TEST_F(PathTransformerTest, RfcNormalize) {
  std::string path_transformation_config = R"EOF(
      operations:
      - normalize_path_rfc_3986: {}
)EOF";
  setPathTransformer(path_transformation_config);
  PathTransformer const& path_transformer = pathTransformer();
  EXPECT_EQ("/x/y/z", path_transformer.transform("/x/y/z", action_)
                          .value()); // Already normalized path don't change.

  EXPECT_EQ("/a/c", path_transformer.transform("a/b/../c", action_).value());   // parent dir
  EXPECT_EQ("/a/b/c", path_transformer.transform("/a/b/./c", action_).value()); // current dir
  EXPECT_EQ("/a/c", path_transformer.transform("a/b/../c", action_).value());   // non / start
  EXPECT_EQ("/c",
            path_transformer.transform("/a/b/../../../../c", action_).value()); // out number parent
  EXPECT_EQ("/c",
            path_transformer.transform("/a/..\\c", action_).value()); // "..\\" canonicalization
  EXPECT_EQ("/%c0%af", path_transformer.transform("/%c0%af", action_)
                           .value()); // 2 bytes unicode reserved characters
  EXPECT_EQ("/%5c%25",
            path_transformer.transform("/%5c%25", action_).value()); // reserved characters
  EXPECT_EQ("/a/c", path_transformer.transform("/a/b/%2E%2E/c", action_).value()); // %2E escape

  EXPECT_EQ("/A/B/C", path_transformer.transform("/A/B/C", action_).value());      // empty
  EXPECT_EQ("/a/c", path_transformer.transform("/a/b/%2E%2E/c", action_).value()); // relative
  EXPECT_EQ("/a/c",
            path_transformer.transform("/a/b/%2e%2e/c", action_).value()); // ends with slash
  EXPECT_EQ("/a/%2F%2f/c",
            path_transformer.transform("/a/%2F%2f/c", action_).value()); // relative ends with slash

  EXPECT_FALSE(path_transformer.transform("/xyz/.%00../abc", action_).has_value());
  EXPECT_FALSE(path_transformer.transform("/xyz/%00.%00./abc", action_).has_value());
  EXPECT_FALSE(path_transformer.transform("/xyz/AAAAA%%0000/abc", action_).has_value());
}

TEST_F(PathTransformerTest, UnescapeSlashes) {
  EXPECT_EQ("",
            PathTransformer::unescapeSlashes("").value()); // empty
  EXPECT_EQ("//",
            PathTransformer::unescapeSlashes("%2f%2F").value()); // case-insensitive
  EXPECT_EQ("/a/b/c/",
            PathTransformer::unescapeSlashes("/a%2Fb%2fc/").value()); // between other characters
  EXPECT_EQ("%2b",
            PathTransformer::unescapeSlashes("%2b").value()); // not %2f
  EXPECT_EQ("/a/b/c",
            PathTransformer::unescapeSlashes("/a/b/c").value()); // not %2f
  EXPECT_EQ("%2",
            PathTransformer::unescapeSlashes("%2").value()); // incomplete
  EXPECT_EQ("%",
            PathTransformer::unescapeSlashes("%").value()); // incomplete
  EXPECT_EQ("/abc%2",
            PathTransformer::unescapeSlashes("/abc%2").value()); // incomplete
  EXPECT_EQ("foo%",
            PathTransformer::unescapeSlashes("foo%").value()); // incomplete
  EXPECT_EQ("/a/",
            PathTransformer::unescapeSlashes("/a%2F").value()); // prefixed
  EXPECT_EQ("/a/",
            PathTransformer::unescapeSlashes("%2fa/").value()); // suffixed
  EXPECT_EQ("%/a/",
            PathTransformer::unescapeSlashes("%%2fa/").value()); // double escape
  EXPECT_EQ("%2/a/",
            PathTransformer::unescapeSlashes("%2%2fa/").value()); // incomplete escape

  EXPECT_EQ("\\\\",
            PathTransformer::unescapeSlashes("%5c%5C").value()); // case-insensitive
  EXPECT_EQ("/a\\b\\c/",
            PathTransformer::unescapeSlashes("/a%5Cb%5cc/").value()); // between other characters
  EXPECT_EQ("/a\\",
            PathTransformer::unescapeSlashes("/a%5C").value()); // prefixed
  EXPECT_EQ("\\a/",
            PathTransformer::unescapeSlashes("%5ca/").value()); // suffixed
  EXPECT_EQ("/x/%2E%2e/z//abc\\../def",
            PathTransformer::unescapeSlashes("/x/%2E%2e/z%2f%2Fabc%5C../def").value());

  EXPECT_EQ("/a\\b/c\\",
            PathTransformer::unescapeSlashes("%2fa%5Cb%2fc%5c").value()); // %5c and %2f together
  EXPECT_EQ("/a\\b/c\\?%2fabcd%5C%%2f%",
            PathTransformer::unescapeSlashes("%2fa%5Cb%2fc%5c?%2fabcd%5C%%2f%")
                .value()); // query is untouched
}

TEST_F(PathTransformerTest, SetNormalizePathAction) {
  // If an operation change the path, the action_ will be set.
  std::string path_transformation_config = R"EOF(
      operations:
      - normalize_path_rfc_3986: {}
        normalize_path_action: REDIRECT
)EOF";
  setPathTransformer(path_transformation_config);
  action_ = NormalizePathAction::Continue;
  PathTransformer path_transformer = pathTransformer();
  // Path is not modified, don't set action_.
  EXPECT_EQ("/x/y/z", path_transformer.transform("/x/y/z", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Continue);
  // Path is modified, set action_.
  EXPECT_EQ("/a/c", path_transformer.transform("a/b/../c", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Redirect);

  path_transformation_config = R"EOF(
      operations:
      - normalize_path_rfc_3986: {}
        normalize_path_action: REJECT
)EOF";
  setPathTransformer(path_transformation_config);
  action_ = NormalizePathAction::Continue;
  path_transformer = pathTransformer();
  // Path is not modified, don't set action_.
  EXPECT_EQ("/x/y/z", path_transformer.transform("/x/y/z", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Continue);
  // Path is modified, set action_.
  EXPECT_EQ("/a/c", path_transformer.transform("a/b/../c", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Reject);
  action_ = NormalizePathAction::Continue;
  // Validate that path transformer will still perform correctly.
  EXPECT_EQ("/x/y/z", path_transformer.transform("/x/y/z", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Continue);

  path_transformation_config = R"EOF(
      operations:
      - normalize_path_rfc_3986: {}
        normalize_path_action: CONTINUE
      - merge_slashes: {}
        normalize_path_action: REDIRECT
)EOF";
  setPathTransformer(path_transformation_config);
  action_ = NormalizePathAction::Continue;
  path_transformer = pathTransformer();
  // normalize_path_rfc_3986 normalizes the path, action_ should be continue.
  EXPECT_EQ("/a/c", path_transformer.transform("a/b/../c", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Continue);
  // merge_slashes normalizes the path, action_ should be redirect.
  EXPECT_EQ("/a/c", path_transformer.transform("a//c", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Redirect);
  action_ = NormalizePathAction::Continue;
  // Both operation modify the path, action_ should be redirect.
  EXPECT_EQ("/a/c", path_transformer.transform("a//c", action_).value());
  EXPECT_EQ(action_, NormalizePathAction::Redirect);
}

TEST_F(PathTransformerTest, DuplicateTransformation) {
  std::string path_transformation_config = R"EOF(
      operations:
      - normalize_path_rfc_3986: {}
      - normalize_path_rfc_3986: {}
      - merge_slashes: {}
)EOF";
  EXPECT_THROW(setPathTransformer(path_transformation_config), EnvoyException);
  path_transformation_config = R"EOF(
      operations:
      - unescape_slashes: {}
      - merge_slashes: {}
      - merge_slashes: {}
)EOF";
  EXPECT_THROW(setPathTransformer(path_transformation_config), EnvoyException);
}

TEST_F(PathUtilityTest, UnescapeSlashes) {
  using UnescapeResult = std::tuple<std::string, PathUtil::UnescapeSlashesResult>;
  auto unescapeSlashes = [this](const std::string& path_value) {
    auto& path_header = pathHeaderEntry(path_value);
    auto result = PathUtil::unescapeSlashes(headers_);
    auto sanitized_path_value = path_header.value().getStringView();
    return UnescapeResult(std::string(sanitized_path_value), result);
  };
  EXPECT_EQ(UnescapeResult("", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("")); // empty
  EXPECT_EQ(UnescapeResult("//", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%2f%2F")); // case-insensitive
  EXPECT_EQ(UnescapeResult("/a/b/c/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("/a%2Fb%2fc/")); // between other characters
  EXPECT_EQ(UnescapeResult("%2b", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("%2b")); // not %2f
  EXPECT_EQ(UnescapeResult("/a/b/c", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("/a/b/c")); // not %2f
  EXPECT_EQ(UnescapeResult("%2", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("%2")); // incomplete
  EXPECT_EQ(UnescapeResult("%", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("%")); // incomplete
  EXPECT_EQ(UnescapeResult("/abc%2", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("/abc%2")); // incomplete
  EXPECT_EQ(UnescapeResult("foo%", PathUtil::UnescapeSlashesResult::NotFound),
            unescapeSlashes("foo%")); // incomplete
  EXPECT_EQ(UnescapeResult("/a/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("/a%2F")); // prefixed
  EXPECT_EQ(UnescapeResult("/a/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%2fa/")); // suffixed
  EXPECT_EQ(UnescapeResult("%/a/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%%2fa/")); // double escape
  EXPECT_EQ(UnescapeResult("%2/a/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%2%2fa/")); // incomplete escape

  EXPECT_EQ(UnescapeResult("\\\\", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%5c%5C")); // case-insensitive
  EXPECT_EQ(UnescapeResult("/a\\b\\c/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("/a%5Cb%5cc/")); // between other characters
  EXPECT_EQ(UnescapeResult("/a\\", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("/a%5C")); // prefixed
  EXPECT_EQ(UnescapeResult("\\a/", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%5ca/")); // suffixed
  EXPECT_EQ(UnescapeResult("/x/%2E%2e/z//abc\\../def",
                           PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("/x/%2E%2e/z%2f%2Fabc%5C../def"));

  EXPECT_EQ(UnescapeResult("/a\\b/c\\", PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%2fa%5Cb%2fc%5c")); // %5c and %2f together
  EXPECT_EQ(UnescapeResult("/a\\b/c\\?%2fabcd%5C%%2f%",
                           PathUtil::UnescapeSlashesResult::FoundAndUnescaped),
            unescapeSlashes("%2fa%5Cb%2fc%5c?%2fabcd%5C%%2f%")); // query is untouched
}

} // namespace Http
} // namespace Envoy
