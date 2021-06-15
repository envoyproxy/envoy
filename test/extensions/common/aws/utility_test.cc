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
  std::string path = "";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringSlash) {
  std::string path = "/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringSlashes) {
  std::string path = "///";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringPrefixSlash) {
  std::string path = "test";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringSuffixSlash) {
  std::string path = "test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringNormalizeSlash) {
  std::string path = "test////test///";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test/test/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringWithEncoding) {
  std::string path = "test$file.txt";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test%24file.txt", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringWithEncodingSpaces) {
  std::string path = "/test and test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test%20and%20test/", canonical_path);
}

TEST(UtilityTest, CanonicalizePathStringWithAlreadyEncodedSpaces) {
  std::string path = "/test%20and%20test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "appmesh");
  EXPECT_EQ("/test%2520and%2520test/", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringDoNotNormalizeSlash) {
  std::string path = "/test//test///";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("/test//test///", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringSlashes) {
  std::string path = "///";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("///", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringWithEncoding) {
  std::string path = "/test$file.txt";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("/test%24file.txt", canonical_path);
}

TEST(UtilityTest, CanonicalizeS3PathStringWithEncodingSpaces) {
  std::string path = "/test and test/";
  const auto canonical_path = Utility::canonicalizePathString(path, "s3");
  EXPECT_EQ("/test%20and%20test/", canonical_path);
}

TEST(UtilityTest, EncodePathSegment) {
  std::string path = "test^!@=-_~.";
  const auto encoded_path = Utility::encodePathSegment(path, "appmesh");
  EXPECT_EQ("test%5E%21%40%3D-_~.", encoded_path);
}

TEST(UtilityTest, EncodeS3PathSegment) {
  std::string path = "/test/^!@=/-_~.";
  const auto encoded_path = Utility::encodePathSegment(path, "s3");
  EXPECT_EQ("/test/%5E%21%40%3D/-_~.", encoded_path);
}

TEST(UtilityTest, CanonicalizeQueryString) {
  std::string query = "a=1&b=2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1&b=2", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringTrailingEquals) {
  std::string query = "a&b";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=&b=", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringSorted) {
  std::string query = "a=3&b=1&a=2&a=1";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1&a=2&a=3&b=1", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringEncoded) {
  std::string query = "a=^!@&b=/-_~.";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=%5E%21%40&b=%2F-_~.", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringWithPlus) {
  std::string query = "a=1+2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1%202", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringDoubleEncodeEquals) {
  std::string query = "a=!.!=!";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=%21.%21%253D%21", canonical_query);
}

TEST(UtilityTest, EncodeQuerySegment) {
  std::string query = "^!@/-_~.";
  const auto encoded_query = Utility::encodeQueryParam(query);
  EXPECT_EQ("%5E%21%40%2F-_~.", encoded_query);
}

TEST(UtilityTest, EncodeQuerySegmentReserved) {
  std::string query = "?=&";
  const auto encoded_query = Utility::encodeQueryParam(query);
  EXPECT_EQ("%3F%253D%26", encoded_query);
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
