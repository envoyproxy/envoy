#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "library/cc/request_headers_builder.h"

namespace Envoy {
namespace Platform {
namespace {

TEST(RequestHeadersBuilderTest, ConstructsFromPieces) {
  RequestHeadersBuilder builder(RequestMethod::POST, "https", "www.example.com", "/");
  RequestHeaders headers = builder.build();
  EXPECT_EQ(RequestMethod::POST, headers.requestMethod());
  EXPECT_EQ("https", headers.scheme());
  EXPECT_EQ("www.example.com", headers.authority());
  EXPECT_EQ("/", headers.path());
}

TEST(RequestHeadersBuilderTest, ConstructsFromUrl) {
  RequestHeadersBuilder builder(RequestMethod::POST, "https://www.example.com/");
  RequestHeaders headers = builder.build();
  EXPECT_EQ(RequestMethod::POST, headers.requestMethod());
  EXPECT_EQ("https", headers.scheme());
  EXPECT_EQ("www.example.com", headers.authority());
  EXPECT_EQ("/", headers.path());
}

TEST(RequestHeadersBuilderTest, ConstructsFromInvalidUrl) {
  RequestHeadersBuilder builder(RequestMethod::POST, "root@example.com");
  RequestHeaders headers = builder.build();
  EXPECT_EQ(RequestMethod::POST, headers.requestMethod());
  EXPECT_EQ("", headers.scheme());
  EXPECT_EQ("", headers.authority());
  EXPECT_EQ("", headers.path());
}

} // namespace
} // namespace Platform
} // namespace Envoy
