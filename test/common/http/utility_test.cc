#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

namespace Http {

// Satisfy linker
const uint64_t CodecOptions::NoCompression;

TEST(HttpUtility, parseQueryString) {
  EXPECT_EQ(Utility::QueryParams(), Utility::parseQueryString("/hello"));
  EXPECT_EQ(Utility::QueryParams(), Utility::parseQueryString("/hello?"));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}}), Utility::parseQueryString("/hello?hello"));
  EXPECT_EQ(Utility::QueryParams({{"hello", "world"}}),
            Utility::parseQueryString("/hello?hello=world"));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}}), Utility::parseQueryString("/hello?hello="));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}}), Utility::parseQueryString("/hello?hello=&"));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}, {"hello2", "world2"}}),
            Utility::parseQueryString("/hello?hello=&hello2=world2"));
  EXPECT_EQ(Utility::QueryParams({{"name", "admin"}, {"level", "trace"}}),
            Utility::parseQueryString("/logging?name=admin&level=trace"));
}

TEST(HttpUtility, getResponseStatus) {
  EXPECT_THROW(Utility::getResponseStatus(HeaderMapImpl{}), CodecClientException);
  EXPECT_EQ(200U, Utility::getResponseStatus(HeaderMapImpl{{":status", "200"}}));
}

TEST(HttpUtility, isInternalRequest) {
  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{}));
  EXPECT_FALSE(Utility::isInternalRequest(
      HeaderMapImpl{{"x-forwarded-for", "10.0.0.1"}, {"x-forwarded-for", "10.0.0.2"}}));
  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "10.0.0.1,10.0.0.2"}}));
  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "50.0.0.1"}}));
  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "blah"}}));

  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "10.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "10.255.255.255"}}));

  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "172.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "172.16.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "172.31.255.255"}}));
  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "172.32.0.0"}}));

  EXPECT_FALSE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "192.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "192.168.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "192.168.255.255"}}));

  EXPECT_TRUE(Utility::isInternalRequest(HeaderMapImpl{{"x-forwarded-for", "127.0.0.1"}}));
}

TEST(HttpUtility, appendXff) {
  {
    HeaderMapImpl headers;
    Utility::appendXff(headers, "127.0.0.1");
    EXPECT_EQ("127.0.0.1", headers.get("x-forwarded-for"));
  }

  {
    HeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Utility::appendXff(headers, "127.0.0.1");
    EXPECT_EQ("10.0.0.1, 127.0.0.1", headers.get("x-forwarded-for"));
  }
}

TEST(HttpUtility, createSslRedirectPath) {
  {
    HeaderMapImpl headers{{":authority", "www.lyft.com"}, {":path", "/hello"}};
    EXPECT_EQ("https://www.lyft.com/hello", Utility::createSslRedirectPath(headers));
  }
}

TEST(HttpUtility, parseCodecOptions) {
  {
    Json::StringLoader json("{}");
    EXPECT_EQ(0UL, Utility::parseCodecOptions(json));
  }

  {
    Json::StringLoader json("{\"http_codec_options\": \"no_compression\"}");
    EXPECT_EQ(CodecOptions::NoCompression, Utility::parseCodecOptions(json));
  }

  {
    Json::StringLoader json("{\"http_codec_options\": \"foo\"}");
    EXPECT_THROW(Utility::parseCodecOptions(json), EnvoyException);
  }
}

TEST(HttpUtility, TwoAddressesInXFF) {
  const std::string first_address = "34.0.0.1";
  const std::string second_address = "10.0.0.1";
  HeaderMapImpl request_headers{
      {"x-forwarded-for", fmt::format("{0},{1}", first_address, second_address)}};
  EXPECT_EQ(second_address, Utility::getLastAddressFromXFF(request_headers));
}

TEST(HttpUtility, EmptyXFF) {
  HeaderMapImpl request_headers;
  EXPECT_EQ("", Utility::getLastAddressFromXFF(request_headers));
}

TEST(HttpUtility, OneAddressInXFF) {
  const std::string first_address = "34.0.0.1";
  HeaderMapImpl request_headers{{"x-forwarded-for", first_address}};
  EXPECT_EQ(first_address, Utility::getLastAddressFromXFF(request_headers));
}

} // Http
