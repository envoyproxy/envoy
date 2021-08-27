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

// Invalid paths are rejected.
TEST_F(PathUtilityTest, InvalidPaths) {
  const std::vector<std::string> invalid_paths{"/xyz/.%00../abc", "/xyz/%00.%00./abc",
                                               "/xyz/AAAAA%%0000/abc"};
  for (const auto& path : invalid_paths) {
    pathHeaderEntry(path);
    EXPECT_FALSE(PathUtil::canonicalPath(headers_)) << "original path: " << path;
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
      {"/a/b/%2E%2E/c", "/a/c"}     // %2E escape
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
