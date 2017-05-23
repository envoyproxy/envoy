#include <cstdint>
#include <string>

#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Http {

// Satisfy linker
const uint64_t Http2Settings::CodecOptions::DISABLE_DYNAMIC_HPACK_TABLE;
const uint32_t Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS;
const uint32_t Http2Settings::DEFAULT_INITIAL_WINDOW_SIZE;

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
  EXPECT_THROW(Utility::getResponseStatus(TestHeaderMapImpl{}), CodecClientException);
  EXPECT_EQ(200U, Utility::getResponseStatus(TestHeaderMapImpl{{":status", "200"}}));
}

TEST(HttpUtility, isInternalRequest) {
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{}));
  EXPECT_FALSE(
      Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "10.0.0.1,10.0.0.2"}}));
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "50.0.0.1"}}));
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "blah"}}));

  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "10.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "10.255.255.255"}}));

  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.16.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.31.255.255"}}));
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.32.0.0"}}));

  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "192.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "192.168.0.0"}}));
  EXPECT_TRUE(
      Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "192.168.255.255"}}));

  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "127.0.0.1"}}));
}

TEST(HttpUtility, appendXff) {
  {
    TestHeaderMapImpl headers;
    Network::Address::Ipv4Instance address("127.0.0.1");
    Utility::appendXff(headers, address);
    EXPECT_EQ("127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Network::Address::Ipv4Instance address("127.0.0.1");
    Utility::appendXff(headers, address);
    EXPECT_EQ("10.0.0.1, 127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Network::Address::PipeInstance address("/foo");
    Utility::appendXff(headers, address);
    EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  }
}

TEST(HttpUtility, createSslRedirectPath) {
  {
    TestHeaderMapImpl headers{{":authority", "www.lyft.com"}, {":path", "/hello"}};
    EXPECT_EQ("https://www.lyft.com/hello", Utility::createSslRedirectPath(headers));
  }
}

TEST(HttpUtility, parseHttp2Settings) {
  {
    Json::ObjectSharedPtr json = Json::Factory::loadFromString("{}");
    EXPECT_EQ(0UL, Utility::parseHttp2Settings(*json).codec_options_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS,
              Utility::parseHttp2Settings(*json).max_concurrent_streams_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_WINDOW_SIZE,
              Utility::parseHttp2Settings(*json).initial_window_size_);
  }

  {
    Json::ObjectSharedPtr json = Json::Factory::loadFromString(R"raw({
                                          "http_codec_options": "no_compression",
                                          "http2_settings" : {
                                            "max_concurrent_streams": 1234,
                                            "initial_window_size": 5678
                                          }
                                        })raw");
    EXPECT_EQ(Http2Settings::CodecOptions::DISABLE_DYNAMIC_HPACK_TABLE,
              Utility::parseHttp2Settings(*json).codec_options_);
    EXPECT_EQ(1234U, Utility::parseHttp2Settings(*json).max_concurrent_streams_);
    EXPECT_EQ(5678U, Utility::parseHttp2Settings(*json).initial_window_size_);
  }

  {
    Json::ObjectSharedPtr json = Json::Factory::loadFromString("{\"http_codec_options\": \"foo\"}");
    EXPECT_THROW_WITH_MESSAGE(Utility::parseHttp2Settings(*json), EnvoyException,
                              "unknown http codec option 'foo'");
  }
}

TEST(HttpUtility, TwoAddressesInXFF) {
  const std::string first_address = "34.0.0.1";
  const std::string second_address = "10.0.0.1";
  TestHeaderMapImpl request_headers{
      {"x-forwarded-for", fmt::format("{0}, {0}, {1}", first_address, second_address)}};
  EXPECT_EQ(second_address, Utility::getLastAddressFromXFF(request_headers));
}

TEST(HttpUtility, EmptyXFF) {
  {
    TestHeaderMapImpl request_headers{{"x-forwarded-for", ""}};
    EXPECT_EQ("", Utility::getLastAddressFromXFF(request_headers));
  }

  {
    TestHeaderMapImpl request_headers;
    EXPECT_EQ("", Utility::getLastAddressFromXFF(request_headers));
  }
}

TEST(HttpUtility, OneAddressInXFF) {
  const std::string first_address = "34.0.0.1";
  TestHeaderMapImpl request_headers{{"x-forwarded-for", first_address}};
  EXPECT_EQ(first_address, Utility::getLastAddressFromXFF(request_headers));
}

TEST(HttpUtility, TestParseCookie) {
  TestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "somekey=somevalue; someotherkey=someothervalue"},
      {"cookie", "abc=def; token=abc123; Expires=Wed, 09 Jun 2021 10:18:14 GMT"},
      {"cookie", "key2=value2; key3=value3"}};

  std::string key{"token"};
  std::string value = Utility::parseCookieValue(headers, key);
  EXPECT_EQ(value, "abc123");
}

TEST(HttpUtility, TestParseCookieBadValues) {
  TestHeaderMapImpl headers{{"cookie", "token1=abc123; = "},
                            {"cookie", "token2=abc123;   "},
                            {"cookie", "; token3=abc123;"},
                            {"cookie", "=; token4=\"abc123\""}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "token1"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token2"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token3"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token4"), "abc123");
}

TEST(HttpUtility, TestParseCookieWithQuotes) {
  TestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "dquote=\"; quoteddquote=\"\"\""},
      {"cookie", "leadingdquote=\"foobar;"},
      {"cookie", "abc=def; token=\"abc123\"; Expires=Wed, 09 Jun 2021 10:18:14 GMT"}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "token"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "dquote"), "\"");
  EXPECT_EQ(Utility::parseCookieValue(headers, "quoteddquote"), "\"");
  EXPECT_EQ(Utility::parseCookieValue(headers, "leadingdquote"), "\"foobar");
}

} // Http
} // Envoy
