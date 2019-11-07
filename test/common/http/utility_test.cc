#include <cstdint>
#include <string>

#include "envoy/api/v2/core/protocol.pb.h"
#include "envoy/api/v2/core/protocol.pb.validate.h"

#include "common/common/fmt.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;

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
  EXPECT_FALSE(Utility::isUpgrade(
      TestHeaderMapImpl{{"Connection", "IsNotAnUpgrade"}, {"Upgrade", "websocket"}}));

  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"Connection", "upgrade"}, {"Upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"connection", "upgrade"}, {"upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"connection", "Upgrade"}, {"upgrade", "WebSocket"}}));
}

TEST(HttpUtility, isUpgrade) {
  EXPECT_FALSE(Utility::isUpgrade(TestHeaderMapImpl{}));
  EXPECT_FALSE(Utility::isUpgrade(TestHeaderMapImpl{{"connection", "upgrade"}}));
  EXPECT_FALSE(Utility::isUpgrade(TestHeaderMapImpl{{"upgrade", "foo"}}));
  EXPECT_FALSE(Utility::isUpgrade(TestHeaderMapImpl{{"Connection", "close"}, {"Upgrade", "foo"}}));
  EXPECT_FALSE(
      Utility::isUpgrade(TestHeaderMapImpl{{"Connection", "IsNotAnUpgrade"}, {"Upgrade", "foo"}}));
  EXPECT_FALSE(Utility::isUpgrade(
      TestHeaderMapImpl{{"Connection", "Is Not An Upgrade"}, {"Upgrade", "foo"}}));

  EXPECT_TRUE(Utility::isUpgrade(TestHeaderMapImpl{{"Connection", "upgrade"}, {"Upgrade", "foo"}}));
  EXPECT_TRUE(Utility::isUpgrade(TestHeaderMapImpl{{"connection", "upgrade"}, {"upgrade", "foo"}}));
  EXPECT_TRUE(Utility::isUpgrade(TestHeaderMapImpl{{"connection", "Upgrade"}, {"upgrade", "FoO"}}));
  EXPECT_TRUE(Utility::isUpgrade(
      TestHeaderMapImpl{{"connection", "keep-alive, Upgrade"}, {"upgrade", "FOO"}}));
}

// Start with H1 style websocket request headers. Transform to H2 and back.
TEST(HttpUtility, H1H2H1Request) {
  TestHeaderMapImpl converted_headers = {
      {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};
  const TestHeaderMapImpl original_headers(converted_headers);

  ASSERT_TRUE(Utility::isUpgrade(converted_headers));
  ASSERT_FALSE(Utility::isH2UpgradeRequest(converted_headers));
  Utility::transformUpgradeRequestFromH1toH2(converted_headers);

  ASSERT_FALSE(Utility::isUpgrade(converted_headers));
  ASSERT_TRUE(Utility::isH2UpgradeRequest(converted_headers));
  Utility::transformUpgradeRequestFromH2toH1(converted_headers);

  ASSERT_TRUE(Utility::isUpgrade(converted_headers));
  ASSERT_FALSE(Utility::isH2UpgradeRequest(converted_headers));
  ASSERT_EQ(converted_headers, original_headers);
}

// Start with H2 style websocket request headers. Transform to H1 and back.
TEST(HttpUtility, H2H1H2Request) {
  TestHeaderMapImpl converted_headers = {{":method", "CONNECT"}, {":protocol", "websocket"}};
  const TestHeaderMapImpl original_headers(converted_headers);

  ASSERT_FALSE(Utility::isUpgrade(converted_headers));
  ASSERT_TRUE(Utility::isH2UpgradeRequest(converted_headers));
  Utility::transformUpgradeRequestFromH2toH1(converted_headers);

  ASSERT_TRUE(Utility::isUpgrade(converted_headers));
  ASSERT_FALSE(Utility::isH2UpgradeRequest(converted_headers));
  Utility::transformUpgradeRequestFromH1toH2(converted_headers);

  ASSERT_FALSE(Utility::isUpgrade(converted_headers));
  ASSERT_TRUE(Utility::isH2UpgradeRequest(converted_headers));
  converted_headers.removeContentLength();
  ASSERT_EQ(converted_headers, original_headers);
}

// Start with H1 style websocket response headers. Transform to H2 and back.
TEST(HttpUtility, H1H2H1Response) {
  TestHeaderMapImpl converted_headers = {
      {":status", "101"}, {"upgrade", "websocket"}, {"connection", "upgrade"}};
  const TestHeaderMapImpl original_headers(converted_headers);

  ASSERT_TRUE(Utility::isUpgrade(converted_headers));
  Utility::transformUpgradeResponseFromH1toH2(converted_headers);

  ASSERT_FALSE(Utility::isUpgrade(converted_headers));
  Utility::transformUpgradeResponseFromH2toH1(converted_headers, "websocket");

  ASSERT_TRUE(Utility::isUpgrade(converted_headers));
  ASSERT_EQ(converted_headers, original_headers);
}

// Users of the transformation functions should not expect the results to be
// identical. Because the headers are always added in a set order, the original
// header order may not be preserved.
TEST(HttpUtility, OrderNotPreserved) {
  TestHeaderMapImpl expected_headers = {
      {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};

  TestHeaderMapImpl converted_headers = {
      {":method", "GET"}, {"Connection", "upgrade"}, {"Upgrade", "foo"}};

  Utility::transformUpgradeRequestFromH1toH2(converted_headers);
  Utility::transformUpgradeRequestFromH2toH1(converted_headers);
  EXPECT_EQ(converted_headers, expected_headers);
}

// A more serious problem with using WebSocket help for general Upgrades, is that method for
// WebSocket is always GET but the method for other upgrades is allowed to be a
// POST. This is a documented weakness in Envoy docs and can be addressed with
// a custom x-envoy-original-method header if it is ever needed.
TEST(HttpUtility, MethodNotPreserved) {
  TestHeaderMapImpl expected_headers = {
      {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};

  TestHeaderMapImpl converted_headers = {
      {":method", "POST"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};

  Utility::transformUpgradeRequestFromH1toH2(converted_headers);
  Utility::transformUpgradeRequestFromH2toH1(converted_headers);
  EXPECT_EQ(converted_headers, expected_headers);
}

TEST(HttpUtility, ContentLengthMangling) {
  // Content-Length of 0 is removed on the request path.
  {
    TestHeaderMapImpl request_headers = {
        {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}, {"content-length", "0"}};
    Utility::transformUpgradeRequestFromH1toH2(request_headers);
    EXPECT_TRUE(request_headers.ContentLength() == nullptr);
  }

  // Non-zero Content-Length is not removed on the request path.
  {
    TestHeaderMapImpl request_headers = {
        {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}, {"content-length", "1"}};
    Utility::transformUpgradeRequestFromH1toH2(request_headers);
    EXPECT_FALSE(request_headers.ContentLength() == nullptr);
  }

  // Content-Length of 0 is removed on the response path.
  {
    TestHeaderMapImpl response_headers = {{":status", "101"},
                                          {"upgrade", "websocket"},
                                          {"connection", "upgrade"},
                                          {"content-length", "0"}};
    Utility::transformUpgradeResponseFromH1toH2(response_headers);
    EXPECT_TRUE(response_headers.ContentLength() == nullptr);
  }

  // Non-zero Content-Length is not removed on the response path.
  {
    TestHeaderMapImpl response_headers = {{":status", "101"},
                                          {"upgrade", "websocket"},
                                          {"connection", "upgrade"},
                                          {"content-length", "1"}};
    Utility::transformUpgradeResponseFromH1toH2(response_headers);
    EXPECT_FALSE(response_headers.ContentLength() == nullptr);
  }
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
    EXPECT_EQ("10.0.0.1,127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Network::Address::PipeInstance address("/foo");
    Utility::appendXff(headers, address);
    EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  }
}

TEST(HttpUtility, appendVia) {
  {
    TestHeaderMapImpl headers;
    Utility::appendVia(headers, "foo");
    EXPECT_EQ("foo", headers.get_("via"));
  }

  {
    TestHeaderMapImpl headers{{"via", "foo"}};
    Utility::appendVia(headers, "bar");
    EXPECT_EQ("foo, bar", headers.get_("via"));
  }
}

TEST(HttpUtility, createSslRedirectPath) {
  {
    TestHeaderMapImpl headers{{":authority", "www.lyft.com"}, {":path", "/hello"}};
    EXPECT_EQ("https://www.lyft.com/hello", Utility::createSslRedirectPath(headers));
  }
}

namespace {

Http2Settings parseHttp2SettingsFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::core::Http2ProtocolOptions http2_protocol_options;
  TestUtility::loadFromYamlAndValidate(yaml, http2_protocol_options);
  return Utility::parseHttp2Settings(http2_protocol_options);
}

} // namespace

TEST(HttpUtility, parseHttp2Settings) {
  {
    auto http2_settings = parseHttp2SettingsFromV2Yaml("{}");
    EXPECT_EQ(Http2Settings::DEFAULT_HPACK_TABLE_SIZE, http2_settings.hpack_table_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS,
              http2_settings.max_concurrent_streams_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE,
              http2_settings.initial_stream_window_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE,
              http2_settings.initial_connection_window_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES, http2_settings.max_outbound_frames_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES,
              http2_settings.max_outbound_control_frames_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD,
              http2_settings.max_consecutive_inbound_frames_with_empty_payload_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM,
              http2_settings.max_inbound_priority_frames_per_stream_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT,
              http2_settings.max_inbound_window_update_frames_per_data_frame_sent_);
  }

  {
    const std::string yaml = R"EOF(
hpack_table_size: 1
max_concurrent_streams: 2
initial_stream_window_size: 65535
initial_connection_window_size: 65535
    )EOF";
    auto http2_settings = parseHttp2SettingsFromV2Yaml(yaml);
    EXPECT_EQ(1U, http2_settings.hpack_table_size_);
    EXPECT_EQ(2U, http2_settings.max_concurrent_streams_);
    EXPECT_EQ(65535U, http2_settings.initial_stream_window_size_);
    EXPECT_EQ(65535U, http2_settings.initial_connection_window_size_);
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
    const std::string first_address = "192.0.2.10";
    const std::string second_address = "192.0.2.1";
    const std::string third_address = "10.0.0.1";
    const std::string fourth_address = "10.0.0.2";
    TestHeaderMapImpl request_headers{
        {"x-forwarded-for", "192.0.2.10, 192.0.2.1 ,10.0.0.1,10.0.0.2"}};

    // No space on the left.
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(fourth_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);

    // No space on either side.
    ret = Utility::getLastAddressFromXFF(request_headers, 1);
    EXPECT_EQ(third_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);

    // Exercise rtrim() and ltrim().
    ret = Utility::getLastAddressFromXFF(request_headers, 2);
    EXPECT_EQ(second_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);

    // No space trimming.
    ret = Utility::getLastAddressFromXFF(request_headers, 3);
    EXPECT_EQ(first_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.single_address_);

    // No address found.
    ret = Utility::getLastAddressFromXFF(request_headers, 4);
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

TEST(HttpUtility, TestMakeSetCookieValue) {
  EXPECT_EQ("name=\"value\"; Max-Age=10",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds(10), false));
  EXPECT_EQ("name=\"value\"",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds::zero(), false));
  EXPECT_EQ("name=\"value\"; Max-Age=10; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds(10), true));
  EXPECT_EQ("name=\"value\"; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds::zero(), true));

  EXPECT_EQ("name=\"value\"; Max-Age=10; Path=/",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds(10), false));
  EXPECT_EQ("name=\"value\"; Path=/",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds::zero(), false));
  EXPECT_EQ("name=\"value\"; Max-Age=10; Path=/; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds(10), true));
  EXPECT_EQ("name=\"value\"; Path=/; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds::zero(), true));
}

TEST(HttpUtility, SendLocalReply) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks, encodeData(_, true));
  Utility::sendLocalReply(false, callbacks, is_reset, Http::Code::PayloadTooLarge, "large",
                          absl::nullopt, false);
}

TEST(HttpUtility, SendLocalGrpcReply) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value().getStringView(), "200");
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.GrpcStatus()->value().getStringView(),
                  std::to_string(enumToInt(Grpc::Status::GrpcStatus::Unknown)));
        EXPECT_NE(headers.GrpcMessage(), nullptr);
        EXPECT_EQ(headers.GrpcMessage()->value().getStringView(), "large");
      }));
  Utility::sendLocalReply(true, callbacks, is_reset, Http::Code::PayloadTooLarge, "large",
                          absl::nullopt, false);
}

TEST(HttpUtility, SendLocalGrpcReplyWithUpstreamJsonPayload) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  const std::string json = R"EOF(
{
    "error": {
        "code": 401,
        "message": "Unauthorized"
    }
}
  )EOF";

  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value().getStringView(), "200");
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.GrpcStatus()->value().getStringView(),
                  std::to_string(enumToInt(Grpc::Status::GrpcStatus::Unauthenticated)));
        EXPECT_NE(headers.GrpcMessage(), nullptr);
        const auto& encoded = Utility::PercentEncoding::encode(json);
        EXPECT_EQ(headers.GrpcMessage()->value().getStringView(), encoded);
      }));
  Utility::sendLocalReply(true, callbacks, is_reset, Http::Code::Unauthorized, json, absl::nullopt,
                          false);
}

TEST(HttpUtility, RateLimitedGrpcStatus) {
  MockStreamDecoderFilterCallbacks callbacks;

  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.GrpcStatus()->value().getStringView(),
                  std::to_string(enumToInt(Grpc::Status::GrpcStatus::Unavailable)));
      }));
  Utility::sendLocalReply(true, callbacks, false, Http::Code::TooManyRequests, "", absl::nullopt,
                          false);

  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.GrpcStatus()->value().getStringView(),
                  std::to_string(enumToInt(Grpc::Status::GrpcStatus::ResourceExhausted)));
      }));
  Utility::sendLocalReply(
      true, callbacks, false, Http::Code::TooManyRequests, "",
      absl::make_optional<Grpc::Status::GrpcStatus>(Grpc::Status::GrpcStatus::ResourceExhausted),
      false);
}

TEST(HttpUtility, SendLocalReplyDestroyedEarly) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, encodeHeaders_(_, false)).WillOnce(InvokeWithoutArgs([&]() -> void {
    is_reset = true;
  }));
  EXPECT_CALL(callbacks, encodeData(_, true)).Times(0);
  Utility::sendLocalReply(false, callbacks, is_reset, Http::Code::PayloadTooLarge, "large",
                          absl::nullopt, false);
}

TEST(HttpUtility, SendLocalReplyHeadRequest) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const HeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.ContentLength()->value().getStringView(),
                  fmt::format("{}", strlen("large")));
      }));
  Utility::sendLocalReply(false, callbacks, is_reset, Http::Code::PayloadTooLarge, "large",
                          absl::nullopt, true);
}

TEST(HttpUtility, TestExtractHostPathFromUri) {
  absl::string_view host, path;

  // FQDN
  Utility::extractHostPathFromUri("scheme://dns.name/x/y/z", host, path);
  EXPECT_EQ(host, "dns.name");
  EXPECT_EQ(path, "/x/y/z");

  // Just the host part
  Utility::extractHostPathFromUri("dns.name", host, path);
  EXPECT_EQ(host, "dns.name");
  EXPECT_EQ(path, "/");

  // Just host and path
  Utility::extractHostPathFromUri("dns.name/x/y/z", host, path);
  EXPECT_EQ(host, "dns.name");
  EXPECT_EQ(path, "/x/y/z");

  // Just the path
  Utility::extractHostPathFromUri("/x/y/z", host, path);
  EXPECT_EQ(host, "");
  EXPECT_EQ(path, "/x/y/z");

  // Some invalid URI
  Utility::extractHostPathFromUri("scheme://adf-scheme://adf", host, path);
  EXPECT_EQ(host, "adf-scheme:");
  EXPECT_EQ(path, "//adf");

  Utility::extractHostPathFromUri("://", host, path);
  EXPECT_EQ(host, "");
  EXPECT_EQ(path, "/");

  Utility::extractHostPathFromUri("/:/adsf", host, path);
  EXPECT_EQ(host, "");
  EXPECT_EQ(path, "/:/adsf");
}

TEST(HttpUtility, TestPrepareHeaders) {
  envoy::api::v2::core::HttpUri http_uri;
  http_uri.set_uri("scheme://dns.name/x/y/z");

  Http::MessagePtr message = Utility::prepareHeaders(http_uri);

  EXPECT_EQ("/x/y/z", message->headers().Path()->value().getStringView());
  EXPECT_EQ("dns.name", message->headers().Host()->value().getStringView());
}

TEST(HttpUtility, QueryParamsToString) {
  EXPECT_EQ("", Utility::queryParamsToString(Utility::QueryParams({})));
  EXPECT_EQ("?a=1", Utility::queryParamsToString(Utility::QueryParams({{"a", "1"}})));
  EXPECT_EQ("?a=1&b=2",
            Utility::queryParamsToString(Utility::QueryParams({{"a", "1"}, {"b", "2"}})));
}

TEST(HttpUtility, ResetReasonToString) {
  EXPECT_EQ("connection failure",
            Utility::resetReasonToString(Http::StreamResetReason::ConnectionFailure));
  EXPECT_EQ("connection termination",
            Utility::resetReasonToString(Http::StreamResetReason::ConnectionTermination));
  EXPECT_EQ("local reset", Utility::resetReasonToString(Http::StreamResetReason::LocalReset));
  EXPECT_EQ("local refused stream reset",
            Utility::resetReasonToString(Http::StreamResetReason::LocalRefusedStreamReset));
  EXPECT_EQ("overflow", Utility::resetReasonToString(Http::StreamResetReason::Overflow));
  EXPECT_EQ("remote reset", Utility::resetReasonToString(Http::StreamResetReason::RemoteReset));
  EXPECT_EQ("remote refused stream reset",
            Utility::resetReasonToString(Http::StreamResetReason::RemoteRefusedStreamReset));
}

// Verify that it resolveMostSpecificPerFilterConfigGeneric works with nil routes.
TEST(HttpUtility, ResolveMostSpecificPerFilterConfigNilRoute) {
  EXPECT_EQ(nullptr, Utility::resolveMostSpecificPerFilterConfigGeneric("envoy.filter", nullptr));
}

class TestConfig : public Router::RouteSpecificFilterConfig {
public:
  int state_;
  void merge(const TestConfig& other) { state_ += other.state_; }
};

// Verify that resolveMostSpecificPerFilterConfig works and we get back the original type.
TEST(HttpUtility, ResolveMostSpecificPerFilterConfig) {
  TestConfig testConfig;

  const std::string filter_name = "envoy.filter";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  // make the file callbacks return our test config
  ON_CALL(*filter_callbacks.route_, perFilterConfig(filter_name))
      .WillByDefault(Return(&testConfig));

  // test the we get the same object back (as this goes through the dynamic_cast)
  auto resolved_filter_config = Utility::resolveMostSpecificPerFilterConfig<TestConfig>(
      filter_name, filter_callbacks.route());
  EXPECT_EQ(&testConfig, resolved_filter_config);
}

// Verify that resolveMostSpecificPerFilterConfigGeneric indeed returns the most specific per filter
// config.
TEST(HttpUtility, ResolveMostSpecificPerFilterConfigGeneric) {
  const std::string filter_name = "envoy.filter";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  const Router::RouteSpecificFilterConfig* nullconfig = nullptr;
  const Router::RouteSpecificFilterConfig* one = nullconfig + 1;
  const Router::RouteSpecificFilterConfig* two = nullconfig + 2;
  const Router::RouteSpecificFilterConfig* three = nullconfig + 3;

  // Test when there's nothing on the route
  EXPECT_EQ(nullptr, Utility::resolveMostSpecificPerFilterConfigGeneric(filter_name,
                                                                        filter_callbacks.route()));

  // Testing in reverse order, so that the method always returns the last object.
  ON_CALL(filter_callbacks.route_->route_entry_.virtual_host_, perFilterConfig(filter_name))
      .WillByDefault(Return(one));
  EXPECT_EQ(one, Utility::resolveMostSpecificPerFilterConfigGeneric(filter_name,
                                                                    filter_callbacks.route()));

  ON_CALL(*filter_callbacks.route_, perFilterConfig(filter_name)).WillByDefault(Return(two));
  EXPECT_EQ(two, Utility::resolveMostSpecificPerFilterConfigGeneric(filter_name,
                                                                    filter_callbacks.route()));

  ON_CALL(filter_callbacks.route_->route_entry_, perFilterConfig(filter_name))
      .WillByDefault(Return(three));
  EXPECT_EQ(three, Utility::resolveMostSpecificPerFilterConfigGeneric(filter_name,
                                                                      filter_callbacks.route()));

  // Cover the case of no route entry
  ON_CALL(*filter_callbacks.route_, routeEntry()).WillByDefault(Return(nullptr));
  EXPECT_EQ(two, Utility::resolveMostSpecificPerFilterConfigGeneric(filter_name,
                                                                    filter_callbacks.route()));
}

// Verify that traversePerFilterConfigGeneric traverses in the order of specificity.
TEST(HttpUtility, TraversePerFilterConfigIteratesInOrder) {
  const std::string filter_name = "envoy.filter";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  // Create configs to test; to ease of testing instead of using real objects
  // we will use pointers that are actually indexes.
  const Router::RouteSpecificFilterConfig* nullconfig = nullptr;
  size_t num_configs = 1;
  ON_CALL(filter_callbacks.route_->route_entry_.virtual_host_, perFilterConfig(filter_name))
      .WillByDefault(Return(nullconfig + num_configs));
  num_configs++;
  ON_CALL(*filter_callbacks.route_, perFilterConfig(filter_name))
      .WillByDefault(Return(nullconfig + num_configs));
  num_configs++;
  ON_CALL(filter_callbacks.route_->route_entry_, perFilterConfig(filter_name))
      .WillByDefault(Return(nullconfig + num_configs));

  // a vector to save which configs are visited by the traversePerFilterConfigGeneric
  std::vector<size_t> visited_configs(num_configs, 0);

  // Iterate; save the retrieved config index in the iteration index in visited_configs.
  size_t index = 0;
  Utility::traversePerFilterConfigGeneric(filter_name, filter_callbacks.route(),
                                          [&](const Router::RouteSpecificFilterConfig& cfg) {
                                            int cfg_index = &cfg - nullconfig;
                                            visited_configs[index] = cfg_index - 1;
                                            index++;
                                          });

  // Make sure all methods were called, and in order.
  for (size_t i = 0; i < visited_configs.size(); i++) {
    EXPECT_EQ(i, visited_configs[i]);
  }
}

// Verify that traversePerFilterConfig works and we get back the original type.
TEST(HttpUtility, TraversePerFilterConfigTyped) {
  TestConfig testConfig;

  const std::string filter_name = "envoy.filter";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  // make the file callbacks return our test config
  ON_CALL(*filter_callbacks.route_, perFilterConfig(filter_name))
      .WillByDefault(Return(&testConfig));

  // iterate the configs
  size_t index = 0;
  Utility::traversePerFilterConfig<TestConfig>(filter_name, filter_callbacks.route(),
                                               [&](const TestConfig&) { index++; });

  // make sure that the callback was called (which means that the dynamic_cast worked.)
  EXPECT_EQ(1, index);
}

// Verify that merging works as expected and we get back the merged result.
TEST(HttpUtility, GetMergedPerFilterConfig) {
  TestConfig baseTestConfig, routeTestConfig;

  baseTestConfig.state_ = 1;
  routeTestConfig.state_ = 1;

  const std::string filter_name = "envoy.filter";
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  // make the file callbacks return our test config
  ON_CALL(filter_callbacks.route_->route_entry_.virtual_host_, perFilterConfig(filter_name))
      .WillByDefault(Return(&baseTestConfig));
  ON_CALL(*filter_callbacks.route_, perFilterConfig(filter_name))
      .WillByDefault(Return(&routeTestConfig));

  // merge the configs
  auto merged_cfg = Utility::getMergedPerFilterConfig<TestConfig>(
      filter_name, filter_callbacks.route(),
      [&](TestConfig& base_cfg, const TestConfig& route_cfg) { base_cfg.merge(route_cfg); });

  // make sure that the callback was called (which means that the dynamic_cast worked.)
  ASSERT_TRUE(merged_cfg.has_value());
  EXPECT_EQ(2, merged_cfg.value().state_);
}

TEST(Url, ParsingFails) {
  Utility::Url url;
  EXPECT_FALSE(url.initialize(""));
  EXPECT_FALSE(url.initialize("foo"));
  EXPECT_FALSE(url.initialize("http://"));
  EXPECT_FALSE(url.initialize("random_scheme://host.com/path"));
}

void ValidateUrl(absl::string_view raw_url, absl::string_view expected_scheme,
                 absl::string_view expected_host_port, absl::string_view expected_path) {
  Utility::Url url;
  ASSERT_TRUE(url.initialize(raw_url)) << "Failed to initialize " << raw_url;
  EXPECT_EQ(url.scheme(), expected_scheme);
  EXPECT_EQ(url.host_and_port(), expected_host_port);
  EXPECT_EQ(url.path_and_query_params(), expected_path);
}

TEST(Url, ParsingTest) {
  // Test url with no explicit path (with and without port)
  ValidateUrl("http://www.host.com", "http", "www.host.com", "/");
  ValidateUrl("http://www.host.com:80", "http", "www.host.com:80", "/");

  // Test url with "/" path.
  ValidateUrl("http://www.host.com:80/", "http", "www.host.com:80", "/");
  ValidateUrl("http://www.host.com/", "http", "www.host.com", "/");

  // Test url with "?".
  ValidateUrl("http://www.host.com:80/?", "http", "www.host.com:80", "/?");
  ValidateUrl("http://www.host.com/?", "http", "www.host.com", "/?");

  // Test url with "?" but without slash.
  ValidateUrl("http://www.host.com:80?", "http", "www.host.com:80", "?");
  ValidateUrl("http://www.host.com?", "http", "www.host.com", "?");

  // Test url with multi-character path
  ValidateUrl("http://www.host.com:80/path", "http", "www.host.com:80", "/path");
  ValidateUrl("http://www.host.com/path", "http", "www.host.com", "/path");

  // Test url with multi-character path and ? at the end
  ValidateUrl("http://www.host.com:80/path?", "http", "www.host.com:80", "/path?");
  ValidateUrl("http://www.host.com/path?", "http", "www.host.com", "/path?");

  // Test https scheme
  ValidateUrl("https://www.host.com", "https", "www.host.com", "/");

  // Test url with query parameter
  ValidateUrl("http://www.host.com:80/?query=param", "http", "www.host.com:80", "/?query=param");
  ValidateUrl("http://www.host.com/?query=param", "http", "www.host.com", "/?query=param");

  // Test url with query parameter but without slash
  ValidateUrl("http://www.host.com:80?query=param", "http", "www.host.com:80", "?query=param");
  ValidateUrl("http://www.host.com?query=param", "http", "www.host.com", "?query=param");

  // Test url with multi-character path and query parameter
  ValidateUrl("http://www.host.com:80/path?query=param", "http", "www.host.com:80",
              "/path?query=param");
  ValidateUrl("http://www.host.com/path?query=param", "http", "www.host.com", "/path?query=param");

  // Test url with multi-character path and more than one query parameter
  ValidateUrl("http://www.host.com:80/path?query=param&query2=param2", "http", "www.host.com:80",
              "/path?query=param&query2=param2");
  ValidateUrl("http://www.host.com/path?query=param&query2=param2", "http", "www.host.com",
              "/path?query=param&query2=param2");
  // Test url with multi-character path, more than one query parameter and fragment
  ValidateUrl("http://www.host.com:80/path?query=param&query2=param2#fragment", "http",
              "www.host.com:80", "/path?query=param&query2=param2#fragment");
  ValidateUrl("http://www.host.com/path?query=param&query2=param2#fragment", "http", "www.host.com",
              "/path?query=param&query2=param2#fragment");
}

void validatePercentEncodingEncodeDecode(absl::string_view source,
                                         absl::string_view expected_encoded) {
  EXPECT_EQ(Utility::PercentEncoding::encode(source), expected_encoded);
  EXPECT_EQ(Utility::PercentEncoding::decode(expected_encoded), source);
}

TEST(PercentEncoding, EncodeDecode) {
  const std::string json = R"EOF(
{
    "error": {
        "code": 401,
        "message": "Unauthorized"
    }
}
  )EOF";
  validatePercentEncodingEncodeDecode(json, "%0A{%0A    \"error\": {%0A        \"code\": 401,%0A   "
                                            "     \"message\": \"Unauthorized\"%0A    }%0A}%0A  ");
  validatePercentEncodingEncodeDecode("too large", "too large");
  validatePercentEncodingEncodeDecode("_-ok-_", "_-ok-_");
}

TEST(PercentEncoding, Trailing) {
  EXPECT_EQ(Utility::PercentEncoding::decode("too%20lar%20"), "too lar ");
  EXPECT_EQ(Utility::PercentEncoding::decode("too%20larg%e"), "too larg%e");
  EXPECT_EQ(Utility::PercentEncoding::decode("too%20large%"), "too large%");
}

} // namespace Http
} // namespace Envoy
