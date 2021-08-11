#include "source/extensions/common/aws/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::Pair;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

// Headers must be in alphabetical order by virtue of std::map
TEST(UtilityTest, CanonicalizeHeadersInAlphabeticalOrder) {
  Http::TestRequestHeaderMapImpl headers{
      {"d", "d_value"}, {"f", "f_value"}, {"b", "b_value"},
      {"e", "e_value"}, {"c", "c_value"}, {"a", "a_value"},
  };
  const auto map = Utility::canonicalizeHeaders(headers);
  EXPECT_THAT(map, ElementsAre(Pair("a", "a_value"), Pair("b", "b_value"), Pair("c", "c_value"),
                               Pair("d", "d_value"), Pair("e", "e_value"), Pair("f", "f_value")));
}

// HTTP pseudo-headers should be ignored
TEST(UtilityTest, CanonicalizeHeadersSkippingPseudoHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {":path", "path_value"},
      {":method", "GET"},
      {"normal", "normal_value"},
  };
  const auto map = Utility::canonicalizeHeaders(headers);
  EXPECT_THAT(map, ElementsAre(Pair("normal", "normal_value")));
}

// Repeated headers are joined with commas
TEST(UtilityTest, CanonicalizeHeadersJoiningDuplicatesWithCommas) {
  Http::TestRequestHeaderMapImpl headers{
      {"a", "a_value1"},
      {"a", "a_value2"},
      {"a", "a_value3"},
  };
  const auto map = Utility::canonicalizeHeaders(headers);
  EXPECT_THAT(map, ElementsAre(Pair("a", "a_value1,a_value2,a_value3")));
}

// We canonicalize the :authority header as host
TEST(UtilityTest, CanonicalizeHeadersAuthorityToHost) {
  Http::TestRequestHeaderMapImpl headers{
      {":authority", "authority_value"},
  };
  const auto map = Utility::canonicalizeHeaders(headers);
  EXPECT_THAT(map, ElementsAre(Pair("host", "authority_value")));
}

// Ports 80 and 443 are omitted from the host headers
TEST(UtilityTest, CanonicalizeHeadersRemovingDefaultPortsFromHost) {
  Http::TestRequestHeaderMapImpl headers_port80{
      {":authority", "example.com:80"},
  };
  const auto map_port80 = Utility::canonicalizeHeaders(headers_port80);
  EXPECT_THAT(map_port80, ElementsAre(Pair("host", "example.com")));

  Http::TestRequestHeaderMapImpl headers_port443{
      {":authority", "example.com:443"},
  };
  const auto map_port443 = Utility::canonicalizeHeaders(headers_port443);
  EXPECT_THAT(map_port443, ElementsAre(Pair("host", "example.com")));
}

// Whitespace is trimmed from headers
TEST(UtilityTest, CanonicalizeHeadersTrimmingWhitespace) {
  Http::TestRequestHeaderMapImpl headers{
      {"leading", "    leading value"},
      {"trailing", "trailing value    "},
      {"internal", "internal    value"},
      {"all", "    all    value    "},
  };
  const auto map = Utility::canonicalizeHeaders(headers);
  EXPECT_THAT(map,
              ElementsAre(Pair("all", "all value"), Pair("internal", "internal value"),
                          Pair("leading", "leading value"), Pair("trailing", "trailing value")));
}

// Headers that are likely to mutate are not considered canonical
TEST(UtilityTest, CanonicalizeHeadersDropMutatingHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {":authority", "example.com"},          {"x-forwarded-for", "1.2.3.4"},
      {"x-forwarded-proto", "https"},         {"x-amz-date", "20130708T220855Z"},
      {"x-amz-content-sha256", "e3b0c44..."},
  };
  const auto map = Utility::canonicalizeHeaders(headers);
  EXPECT_THAT(map,
              ElementsAre(Pair("host", "example.com"), Pair("x-amz-content-sha256", "e3b0c44..."),
                          Pair("x-amz-date", "20130708T220855Z")));
}

// Verify the format of a minimalist canonical request
TEST(UtilityTest, MinimalCanonicalRequest) {
  std::map<std::string, std::string> headers;
  const auto request =
      Utility::createCanonicalRequest("appmesh", "GET", "", headers, "content-hash");
  EXPECT_EQ(R"(GET
/



content-hash)",
            request);
}

TEST(UtilityTest, CanonicalRequestWithQueryString) {
  const std::map<std::string, std::string> headers;
  const auto request =
      Utility::createCanonicalRequest("appmesh", "GET", "?query", headers, "content-hash");
  EXPECT_EQ(R"(GET
/
query=


content-hash)",
            request);
}

TEST(UtilityTest, CanonicalRequestWithHeaders) {
  const std::map<std::string, std::string> headers = {
      {"header1", "value1"},
      {"header2", "value2"},
      {"header3", "value3"},
  };
  const auto request =
      Utility::createCanonicalRequest("appmesh", "GET", "", headers, "content-hash");
  EXPECT_EQ(R"(GET
/

header1:value1
header2:value2
header3:value3

header1;header2;header3
content-hash)",
            request);
}

TEST(UtilityTest, CanonicalizePathStringReturnSlash) {
  const absl::string_view path = "";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringSlash) {
  const absl::string_view path = "/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringSlashes) {
  const absl::string_view path = "///";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringPrefixSlash) {
  const absl::string_view path = "test";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringSuffixSlash) {
  const absl::string_view path = "test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringNormalizeSlash) {
  const absl::string_view path = "test////test///";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test/test/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringWithEncoding) {
  const absl::string_view path = "test$file.txt";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test%24file.txt", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringWithEncodingSpaces) {
  const absl::string_view path = "/test and test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test%20and%20test/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringWithAlreadyEncodedSpaces) {
  const absl::string_view path = "/test%20and%20test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test%2520and%2520test/", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringDoNotNormalizeSlash) {
  const absl::string_view path = "/test//test///";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("/test//test///", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringSlashes) {
  const absl::string_view path = "///";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("///", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringWithEncoding) {
  const absl::string_view path = "/test$file.txt";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("/test%24file.txt", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringWithEncodingSpaces) {
  const absl::string_view path = "/test and test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("/test%20and%20test/", canonical_path);
}

TEST(UtilityTest, EncodePathSegment) {
  const absl::string_view path = "test^!@=-_~.";
  const auto encoded_path = Utility::encodePathSegment(path, "appmesh");
  EXPECT_EQ("test%5E%21%40%3D-_~.", encoded_path);
}

TEST(UtilityTest, EncodeS3PathSegment) {
  const absl::string_view path = "/test/^!@=/-_~.";
  const auto encoded_path = Utility::encodePathSegment(path, "s3");
  EXPECT_EQ("/test/%5E%21%40%3D/-_~.", encoded_path);
}

TEST(UtilityTest, CanonicalizeQueryString) {
  const absl::string_view query = "a=1&b=2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1&b=2", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringTrailingEquals) {
  const absl::string_view query = "a&b";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=&b=", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringSorted) {
  const absl::string_view query = "a=3&b=1&a=2&a=1";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1&a=2&a=3&b=1", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringEncoded) {
  const absl::string_view query = "a=^!@&b=/-_~.";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=%5E%21%40&b=%2F-_~.", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringWithPlus) {
  const absl::string_view query = "a=1+2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1%202", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringDoubleEncodeEquals) {
  const absl::string_view query = "a=!.!=!";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=%21.%21%253D%21", canonical_query);
}

TEST(UtilityTest, EncodeQuerySegment) {
  const absl::string_view query = "^!@/-_~.";
  const auto encoded_query = Utility::encodeQueryParam(query);
  EXPECT_EQ("%5E%21%40%2F-_~.", encoded_query);
}

TEST(UtilityTest, EncodeQuerySegmentReserved) {
  const absl::string_view query = "?=&";
  const auto encoded_query = Utility::encodeQueryParam(query);
  EXPECT_EQ("%3F%253D%26", encoded_query);
}

TEST(UtilityTest, CanonicalizationFuzzTest) {
  std::string fuzz;
  fuzz.reserve(3);
  // Printable ASCII 32 - 126
  for (unsigned char i = 32; i <= 126; i++) {
    fuzz.push_back(i);
    for (unsigned char j = 32; j <= 126; j++) {
      fuzz.push_back(j);
      for (unsigned char k = 32; k <= 126; k++) {
        fuzz.push_back(k);
        Utility::encodePathSegment(fuzz, "s3");
        Utility::canonicalizePathString(fuzz, "appmesh");
        Utility::encodeQueryParam(fuzz);
        Utility::canonicalizeQueryString(fuzz);
        fuzz.pop_back();
      }
      fuzz.pop_back();
    }
    fuzz.pop_back();
  }
}

// Verify headers are joined with ";"
TEST(UtilityTest, JoinCanonicalHeaderNames) {
  std::map<std::string, std::string> headers = {
      {"header1", "value1"},
      {"header2", "value2"},
      {"header3", "value3"},
  };
  const auto names = Utility::joinCanonicalHeaderNames(headers);
  EXPECT_EQ("header1;header2;header3", names);
}

// Verify we return "" when there are no headers
TEST(UtilityTest, JoinCanonicalHeaderNamesWithEmptyMap) {
  std::map<std::string, std::string> headers;
  const auto names = Utility::joinCanonicalHeaderNames(headers);
  EXPECT_EQ("", names);
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
