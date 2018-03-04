#include <cstdint>
#include <string>

#include "common/common/fmt.h"
#include "common/config/protocol_json.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::InvokeWithoutArgs;
using testing::_;

namespace Envoy {
namespace Http {

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

TEST(HttpUtility, isWebSocketUpgradeRequest) {
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestHeaderMapImpl{}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestHeaderMapImpl{{"connection", "upgrade"}}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestHeaderMapImpl{{"upgrade", "websocket"}}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"Connection", "close"}, {"Upgrade", "websocket"}}));

  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"Connection", "upgrade"}, {"Upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"connection", "upgrade"}, {"upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"connection", "Upgrade"}, {"upgrade", "WebSocket"}}));
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

namespace {

Http2Settings parseHttp2SettingsFromJson(const std::string& json_string) {
  envoy::api::v2::core::Http2ProtocolOptions http2_protocol_options;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::ProtocolJson::translateHttp2ProtocolOptions(
      *json_object_ptr->getObject("http2_settings", true), http2_protocol_options);
  return Utility::parseHttp2Settings(http2_protocol_options);
}

} // namespace

TEST(HttpUtility, parseHttp2Settings) {
  {
    auto http2_settings = parseHttp2SettingsFromJson("{}");
    EXPECT_EQ(Http2Settings::DEFAULT_HPACK_TABLE_SIZE, http2_settings.hpack_table_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS,
              http2_settings.max_concurrent_streams_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE,
              http2_settings.initial_stream_window_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE,
              http2_settings.initial_connection_window_size_);
  }

  {
    auto http2_settings = parseHttp2SettingsFromJson(R"raw({
                                          "http2_settings" : {
                                            "hpack_table_size": 1,
                                            "max_concurrent_streams": 2,
                                            "initial_stream_window_size": 3,
                                            "initial_connection_window_size": 4
                                          }
                                        })raw");
    EXPECT_EQ(1U, http2_settings.hpack_table_size_);
    EXPECT_EQ(2U, http2_settings.max_concurrent_streams_);
    EXPECT_EQ(3U, http2_settings.initial_stream_window_size_);
    EXPECT_EQ(4U, http2_settings.initial_connection_window_size_);
  }
}

TEST(HttpUtility, getLastAddressFromXFF) {
  {
    const std::string first_address = "192.0.2.10";
    const std::string second_address = "192.0.2.1";
    const std::string third_address = "10.0.0.1";
    TestHeaderMapImpl request_headers{{"x-forwarded-for", "192.0.2.10, 192.0.2.1, 10.0.0.1"}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(third_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);
    ret = Utility::getLastAddressFromXFF(request_headers, 1);
    EXPECT_EQ(second_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);
    ret = Utility::getLastAddressFromXFF(request_headers, 2);
    EXPECT_EQ(first_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);
    ret = Utility::getLastAddressFromXFF(request_headers, 3);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.single_address_);
  }
  {
    TestHeaderMapImpl request_headers{{"x-forwarded-for", ""}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.single_address_);
  }
  {
    TestHeaderMapImpl request_headers{{"x-forwarded-for", ","}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.single_address_);
  }
  {
    TestHeaderMapImpl request_headers{{"x-forwarded-for", ", "}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.single_address_);
  }
  {
    TestHeaderMapImpl request_headers{{"x-forwarded-for", ", bad"}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.single_address_);
  }
  {
    TestHeaderMapImpl request_headers;
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.single_address_);
  }
  {
    const std::string first_address = "34.0.0.1";
    TestHeaderMapImpl request_headers{{"x-forwarded-for", first_address}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(first_address, ret.address_->ip()->addressAsString());
    EXPECT_TRUE(ret.single_address_);
  }
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

TEST(HttpUtility, TestHasSetCookie) {
  TestHeaderMapImpl headers{{"someheader", "10.0.0.1"},
                            {"set-cookie", "somekey=somevalue"},
                            {"set-cookie", "abc=def; Expires=Wed, 09 Jun 2021 10:18:14 GMT"},
                            {"set-cookie", "key2=value2; Secure"}};

  EXPECT_TRUE(Utility::hasSetCookie(headers, "abc"));
  EXPECT_TRUE(Utility::hasSetCookie(headers, "somekey"));
  EXPECT_FALSE(Utility::hasSetCookie(headers, "ghi"));
}

TEST(HttpUtility, TestHasSetCookieBadValues) {
  TestHeaderMapImpl headers{{"someheader", "10.0.0.1"},
                            {"set-cookie", "somekey =somevalue"},
                            {"set-cookie", "abc"},
                            {"set-cookie", "key2=value2; Secure"}};

  EXPECT_FALSE(Utility::hasSetCookie(headers, "abc"));
  EXPECT_TRUE(Utility::hasSetCookie(headers, "key2"));
}

TEST(HttpUtility, SendLocalReply) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks, encodeData(_, true));
  Utility::sendLocalReply(callbacks, is_reset, Http::Code::PayloadTooLarge, "large");
}

TEST(HttpUtility, SendLocalReplyDestroyedEarly) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, encodeHeaders_(_, false)).WillOnce(InvokeWithoutArgs([&]() -> void {
    is_reset = true;
  }));
  EXPECT_CALL(callbacks, encodeData(_, true)).Times(0);
  Utility::sendLocalReply(callbacks, is_reset, Http::Code::PayloadTooLarge, "large");
}

TEST(HttpUtility, TestAppendHeader) {
  // Test appending to a string with a value.
  {
    HeaderString value1;
    value1.setCopy("some;", 5);
    Utility::appendToHeader(value1, "test");
    EXPECT_EQ(value1, "some;,test");
  }

  // Test appending to an empty string.
  {
    HeaderString value2;
    Utility::appendToHeader(value2, "my tag data");
    EXPECT_EQ(value2, "my tag data");
  }

  // Test empty data case.
  {
    HeaderString value3;
    value3.setCopy("empty", 5);
    Utility::appendToHeader(value3, "");
    EXPECT_EQ(value3, "empty");
  }
}

} // namespace Http
} // namespace Envoy
