#include <array>
#include <cstdint>
#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/core/v3/protocol.pb.validate.h"

#include "source/common/common/fmt.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/settings.h"
#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;

namespace Envoy {
namespace Http {
namespace {

void sendLocalReplyTestHelper(const bool& is_reset, StreamDecoderFilterCallbacks& callbacks,
                              const Utility::LocalReplyData& local_reply_data) {
  absl::string_view details;
  if (callbacks.streamInfo().responseCodeDetails().has_value()) {
    details = callbacks.streamInfo().responseCodeDetails().value();
  };

  Utility::sendLocalReply(
      is_reset,
      Utility::EncodeFunctions{nullptr, nullptr,
                               [&](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
                                 callbacks.encodeHeaders(std::move(headers), end_stream, details);
                               },
                               [&](Buffer::Instance& data, bool end_stream) -> void {
                                 callbacks.encodeData(data, end_stream);
                               }},
      local_reply_data);
}

} // namespace

TEST(HttpUtility, parseQueryString) {
  using Vec = std::vector<std::string>;
  using Map = absl::btree_map<std::string, Vec>;

  auto input = "/hello";
  EXPECT_EQ(Map{}, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(Map{}, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?";
  EXPECT_EQ(Map{}, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(Map{}, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello";
  auto expected = Map{{"hello", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello%26";
  expected = Map{{"hello%26", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{{"hello&", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello=world";
  expected = Map{{"hello", Vec{"world"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello=";
  expected = Map{{"hello", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello%26=";
  expected = Map{{"hello%26", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{{"hello&", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello=&";
  expected = Map{{"hello", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello%26=&";
  expected = Map{{"hello%26", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{{"hello&", Vec{""}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?hello=&hello2=world2";
  expected = Map{{"hello", Vec{""}}, {"hello2", Vec{"world2"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/logging?name=admin&level=trace";
  expected = Map{{"name", Vec{"admin"}}, {"level", Vec{"trace"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?param_value_has_encoded_ampersand=a%26b";
  expected = Map{{"param_value_has_encoded_ampersand", Vec{"a%26b"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{{"param_value_has_encoded_ampersand", Vec{"a&b"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?params_has_encoded_%26=a%26b&ok=1";
  expected = Map{{"params_has_encoded_%26", Vec{"a%26b"}}, {"ok", Vec{"1"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{{"params_has_encoded_&", Vec{"a&b"}}, {"ok", Vec{"1"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  input = "/hello?params_%xy_%%yz=%xy%%yz";
  expected = Map{{"params_%xy_%%yz", Vec{"%xy%%yz"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  // A sample of request path with query strings by Prometheus:
  // https://github.com/envoyproxy/envoy/issues/10926#issuecomment-651085261.
  input = "/stats?filter=%28cluster.upstream_%28rq_total%7Crq_time_sum%7Crq_time_count%7Crq_time_"
          "bucket%7Crq_xx%7Crq_complete%7Crq_active%7Ccx_active%29%29%7C%28server.version%29";
  expected = Map{
      {"filter",
       Vec{"%28cluster.upstream_%28rq_total%7Crq_time_sum%7Crq_time_count%7Crq_time_"
           "bucket%7Crq_xx%7Crq_complete%7Crq_active%7Ccx_active%29%29%7C%28server.version%29"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{
      {"filter", Vec{"(cluster.upstream_(rq_total|rq_time_sum|rq_time_count|rq_time_bucket|rq_xx|"
                     "rq_complete|rq_active|cx_active))|(server.version)"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());

  // Requests with repeating keys
  input = "/foo?a=1&b=2&a=3%264&a=5";
  expected = Map{{"a", Vec{"1", "3%264", "5"}}, {"b", Vec{"2"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseQueryString(input).data());
  expected = Map{{"a", Vec{"1", "3&4", "5"}}, {"b", Vec{"2"}}};
  EXPECT_EQ(expected, Utility::QueryParamsMulti::parseAndDecodeQueryString(input).data());
}

TEST(HttpUtility, stripQueryString) {
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/")), "/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/?")), "/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/?x=1")), "/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/?x=1&y=2")), "/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo")), "/foo");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo?")), "/foo");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo?hello=there")), "/foo");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo?hello=there&good=bye")), "/foo");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/?")), "/foo/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/?x=1")), "/foo/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar")), "/foo/bar");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar?")), "/foo/bar");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar?a=b")), "/foo/bar");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar?a=b&b=c")), "/foo/bar");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar/")), "/foo/bar/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar/?")), "/foo/bar/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar/?x=1")), "/foo/bar/");
  EXPECT_EQ(Utility::stripQueryString(HeaderString("/foo/bar/?x=1&y=2")), "/foo/bar/");
}

TEST(HttpUtility, replaceQueryString) {
  // Replace with nothing
  auto params = Utility::QueryParamsMulti();
  EXPECT_EQ(params.replaceQueryString(HeaderString("/")), "/");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/?")), "/");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/?x=0")), "/");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/a")), "/a");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/a/")), "/a/");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/a/?y=5")), "/a/");
  // Replace with x=1
  params = Utility::QueryParamsMulti::parseQueryString("/?x=1");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/")), "/?x=1");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/?")), "/?x=1");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/?x=0")), "/?x=1");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/a?x=0")), "/a?x=1");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/a/?x=0")), "/a/?x=1");
  // More replacements
  params = Utility::QueryParamsMulti::parseQueryString("/?x=1&z=3");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo")), "/foo?x=1&z=3");
  params = Utility::QueryParamsMulti::parseQueryString("/?x=1&y=5");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo?z=2")), "/foo?x=1&y=5");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo?y=9")), "/foo?x=1&y=5");
  // More path components
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo/bar?")), "/foo/bar?x=1&y=5");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo/bar?y=9&a=b")), "/foo/bar?x=1&y=5");
  params = Utility::QueryParamsMulti::parseQueryString("/?a=b&x=1&y=5");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo/bar?y=11&z=7")), "/foo/bar?a=b&x=1&y=5");
  // Repeating keys
  params = Utility::QueryParamsMulti::parseQueryString("/?a=b&x=1&a=5");
  EXPECT_EQ(params.replaceQueryString(HeaderString("/foo/bar?y=11&z=7")), "/foo/bar?a=b&a=5&x=1");
}

TEST(HttpUtility, testQueryParamModification) {
  auto params = Utility::QueryParamsMulti();
  params.add("a", "1");
  EXPECT_EQ(params.toString(), "?a=1");
  params.add("a", "2");
  EXPECT_EQ(params.toString(), "?a=1&a=2");
  params.add("b", "3");
  EXPECT_EQ(params.toString(), "?a=1&a=2&b=3");
  params.add("c", "4");
  EXPECT_EQ(params.toString(), "?a=1&a=2&b=3&c=4");
  params.overwrite("b", "foo");
  EXPECT_EQ(params.toString(), "?a=1&a=2&b=foo&c=4");
  EXPECT_EQ(params.getFirstValue("a").value(), "1");
  EXPECT_EQ(params.getFirstValue("b").value(), "foo");
  EXPECT_FALSE(params.getFirstValue("d").has_value());
  params.remove("b");
  EXPECT_EQ(params.toString(), "?a=1&a=2&c=4");
  params.overwrite("a", "bar");
  EXPECT_EQ(params.toString(), "?a=bar&c=4");
  params.add("a", "bar2");
  EXPECT_EQ(params.toString(), "?a=bar&a=bar2&c=4");
  params.remove("a");
  EXPECT_EQ(params.toString(), "?c=4");
  params.remove("c");
  EXPECT_EQ(params.toString(), "");
}

TEST(HttpUtility, getResponseStatus) {
  EXPECT_ENVOY_BUG(Utility::getResponseStatus(TestResponseHeaderMapImpl{}),
                   "Details: No status in headers");
  EXPECT_EQ(200U, Utility::getResponseStatus(TestResponseHeaderMapImpl{{":status", "200"}}));
}

TEST(HttpUtility, isWebSocketUpgradeRequest) {
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestRequestHeaderMapImpl{}));
  EXPECT_FALSE(
      Utility::isWebSocketUpgradeRequest(TestRequestHeaderMapImpl{{"connection", "upgrade"}}));
  EXPECT_FALSE(
      Utility::isWebSocketUpgradeRequest(TestRequestHeaderMapImpl{{"upgrade", "websocket"}}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(
      TestRequestHeaderMapImpl{{"Connection", "close"}, {"Upgrade", "websocket"}}));
  EXPECT_FALSE(Utility::isUpgrade(
      TestRequestHeaderMapImpl{{"Connection", "IsNotAnUpgrade"}, {"Upgrade", "websocket"}}));

  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestRequestHeaderMapImpl{{"Connection", "upgrade"}, {"Upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestRequestHeaderMapImpl{{"connection", "upgrade"}, {"upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestRequestHeaderMapImpl{{"connection", "Upgrade"}, {"upgrade", "WebSocket"}}));
}

TEST(HttpUtility, isUpgrade) {
  EXPECT_FALSE(Utility::isUpgrade(TestRequestHeaderMapImpl{}));
  EXPECT_FALSE(Utility::isUpgrade(TestRequestHeaderMapImpl{{"connection", "upgrade"}}));
  EXPECT_FALSE(Utility::isUpgrade(TestRequestHeaderMapImpl{{"upgrade", "foo"}}));
  EXPECT_FALSE(
      Utility::isUpgrade(TestRequestHeaderMapImpl{{"Connection", "close"}, {"Upgrade", "foo"}}));
  EXPECT_FALSE(Utility::isUpgrade(
      TestRequestHeaderMapImpl{{"Connection", "IsNotAnUpgrade"}, {"Upgrade", "foo"}}));
  EXPECT_FALSE(Utility::isUpgrade(
      TestRequestHeaderMapImpl{{"Connection", "Is Not An Upgrade"}, {"Upgrade", "foo"}}));

  EXPECT_TRUE(
      Utility::isUpgrade(TestRequestHeaderMapImpl{{"Connection", "upgrade"}, {"Upgrade", "foo"}}));
  EXPECT_TRUE(
      Utility::isUpgrade(TestRequestHeaderMapImpl{{"connection", "upgrade"}, {"upgrade", "foo"}}));
  EXPECT_TRUE(
      Utility::isUpgrade(TestRequestHeaderMapImpl{{"connection", "Upgrade"}, {"upgrade", "FoO"}}));
  EXPECT_TRUE(Utility::isUpgrade(
      TestRequestHeaderMapImpl{{"connection", "keep-alive, Upgrade"}, {"upgrade", "FOO"}}));
}

// Start with H1 style websocket request headers. Transform to H2 and back.
TEST(HttpUtility, H1H2H1Request) {
  TestRequestHeaderMapImpl converted_headers = {
      {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};
  const TestRequestHeaderMapImpl original_headers(converted_headers);

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
  TestRequestHeaderMapImpl converted_headers = {{":method", "CONNECT"}, {":protocol", "websocket"}};
  const TestRequestHeaderMapImpl original_headers(converted_headers);

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

TEST(HttpUtility, ConnectBytestreamSpecialCased) {
  TestRequestHeaderMapImpl headers = {{":method", "CONNECT"}, {":protocol", "bytestream"}};
  ASSERT_FALSE(Utility::isH2UpgradeRequest(headers));
}

// Start with H1 style websocket response headers. Transform to H2 and back.
TEST(HttpUtility, H1H2H1Response) {
  TestResponseHeaderMapImpl converted_headers = {
      {":status", "101"}, {"upgrade", "websocket"}, {"connection", "upgrade"}};
  const TestResponseHeaderMapImpl original_headers(converted_headers);

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
  TestRequestHeaderMapImpl expected_headers = {
      {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};

  TestRequestHeaderMapImpl converted_headers = {
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
  TestRequestHeaderMapImpl expected_headers = {
      {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};

  TestRequestHeaderMapImpl converted_headers = {
      {":method", "POST"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}};

  Utility::transformUpgradeRequestFromH1toH2(converted_headers);
  Utility::transformUpgradeRequestFromH2toH1(converted_headers);
  EXPECT_EQ(converted_headers, expected_headers);
}

TEST(HttpUtility, ContentLengthMangling) {
  // Content-Length of 0 is removed on the request path.
  {
    TestRequestHeaderMapImpl request_headers = {
        {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}, {"content-length", "0"}};
    Utility::transformUpgradeRequestFromH1toH2(request_headers);
    EXPECT_TRUE(request_headers.ContentLength() == nullptr);
  }

  // Non-zero Content-Length is not removed on the request path.
  {
    TestRequestHeaderMapImpl request_headers = {
        {":method", "GET"}, {"Upgrade", "foo"}, {"Connection", "upgrade"}, {"content-length", "1"}};
    Utility::transformUpgradeRequestFromH1toH2(request_headers);
    EXPECT_FALSE(request_headers.ContentLength() == nullptr);
  }

  // Content-Length of 0 is removed on the response path.
  {
    TestResponseHeaderMapImpl response_headers = {{":status", "101"},
                                                  {"upgrade", "websocket"},
                                                  {"connection", "upgrade"},
                                                  {"content-length", "0"}};
    Utility::transformUpgradeResponseFromH1toH2(response_headers);
    EXPECT_TRUE(response_headers.ContentLength() == nullptr);
  }

  // Non-zero Content-Length is not removed on the response path.
  {
    TestResponseHeaderMapImpl response_headers = {{":status", "101"},
                                                  {"upgrade", "websocket"},
                                                  {"connection", "upgrade"},
                                                  {"content-length", "1"}};
    Utility::transformUpgradeResponseFromH1toH2(response_headers);
    EXPECT_FALSE(response_headers.ContentLength() == nullptr);
  }
}

TEST(HttpUtility, appendXff) {
  {
    TestRequestHeaderMapImpl headers;
    Network::Address::Ipv4Instance address("127.0.0.1");
    Utility::appendXff(headers, address);
    EXPECT_EQ("127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestRequestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Network::Address::Ipv4Instance address("127.0.0.1");
    Utility::appendXff(headers, address);
    EXPECT_EQ("10.0.0.1,127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestRequestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    auto address = *Network::Address::PipeInstance::create("/foo");
    Utility::appendXff(headers, *address);
    EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  }
}

TEST(HttpUtility, appendVia) {
  {
    TestResponseHeaderMapImpl headers;
    Utility::appendVia(headers, "foo");
    EXPECT_EQ("foo", headers.get_("via"));
  }

  {
    TestResponseHeaderMapImpl headers{{"via", "foo"}};
    Utility::appendVia(headers, "bar");
    EXPECT_EQ("foo, bar", headers.get_("via"));
  }
}

TEST(HttpUtility, updateAuthority) {
  {
    TestRequestHeaderMapImpl headers;
    Utility::updateAuthority(headers, "dns.name", true);
    EXPECT_EQ("dns.name", headers.get_(":authority"));
    EXPECT_EQ("", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers;
    Utility::updateAuthority(headers, "dns.name", false);
    EXPECT_EQ("dns.name", headers.get_(":authority"));
    EXPECT_EQ("", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers;
    Utility::updateAuthority(headers, "", true);
    EXPECT_EQ("", headers.get_(":authority"));
    EXPECT_EQ("", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers;
    Utility::updateAuthority(headers, "", false);
    EXPECT_EQ("", headers.get_(":authority"));
    EXPECT_EQ("", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{":authority", "host.com"}};
    Utility::updateAuthority(headers, "dns.name", true);
    EXPECT_EQ("dns.name", headers.get_(":authority"));
    EXPECT_EQ("host.com", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{":authority", "host.com"}};
    Utility::updateAuthority(headers, "dns.name", false);
    EXPECT_EQ("dns.name", headers.get_(":authority"));
    EXPECT_EQ("", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{":authority", "host.com"}};
    Utility::updateAuthority(headers, "", true);
    EXPECT_EQ("", headers.get_(":authority"));
    EXPECT_EQ("host.com", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{":authority", "host.com"}};
    Utility::updateAuthority(headers, "", false);
    EXPECT_EQ("", headers.get_(":authority"));
    EXPECT_EQ("", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{":authority", "dns.name"}, {"x-forwarded-host", "host.com"}};
    Utility::updateAuthority(headers, "newhost.com", true);
    EXPECT_EQ("newhost.com", headers.get_(":authority"));
    EXPECT_EQ("host.com,dns.name", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{":authority", "dns.name"}, {"x-forwarded-host", "host.com"}};
    Utility::updateAuthority(headers, "newhost.com", false);
    EXPECT_EQ("newhost.com", headers.get_(":authority"));
    EXPECT_EQ("host.com", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{"x-forwarded-host", "host.com"}};
    Utility::updateAuthority(headers, "dns.name", true);
    EXPECT_EQ("dns.name", headers.get_(":authority"));
    EXPECT_EQ("host.com", headers.get_("x-forwarded-host"));
  }

  {
    TestRequestHeaderMapImpl headers{{"x-forwarded-host", "host.com"}};
    Utility::updateAuthority(headers, "dns.name", false);
    EXPECT_EQ("dns.name", headers.get_(":authority"));
    EXPECT_EQ("host.com", headers.get_("x-forwarded-host"));
  }

  // Test that we only append to x-forwarded-host if it is not already present.
  {
    TestRequestHeaderMapImpl headers{{":authority", "dns.name"},
                                     {"x-forwarded-host", "host.com,dns.name"}};
    Utility::updateAuthority(headers, "newhost.com", true);
    EXPECT_EQ("newhost.com", headers.get_(":authority"));
    EXPECT_EQ("host.com,dns.name", headers.get_("x-forwarded-host"));
  }
}

TEST(HttpUtility, createSslRedirectPath) {
  {
    TestRequestHeaderMapImpl headers{{":authority", "www.lyft.com"}, {":path", "/hello"}};
    EXPECT_EQ("https://www.lyft.com/hello", Utility::createSslRedirectPath(headers));
  }
}

namespace {

envoy::config::core::v3::Http2ProtocolOptions parseHttp2OptionsFromV3Yaml(const std::string& yaml) {
  envoy::config::core::v3::Http2ProtocolOptions http2_options;
  TestUtility::loadFromYamlAndValidate(yaml, http2_options);
  return ::Envoy::Http2::Utility::initializeAndValidateOptions(http2_options).value();
}

} // namespace

TEST(HttpUtility, parseHttp2Settings) {
  {
    using ::Envoy::Http2::Utility::OptionsLimits;
    auto http2_options = parseHttp2OptionsFromV3Yaml("{}");
    EXPECT_EQ(OptionsLimits::DEFAULT_HPACK_TABLE_SIZE, http2_options.hpack_table_size().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS,
              http2_options.max_concurrent_streams().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE,
              http2_options.initial_stream_window_size().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE,
              http2_options.initial_connection_window_size().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES,
              http2_options.max_outbound_frames().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES,
              http2_options.max_outbound_control_frames().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD,
              http2_options.max_consecutive_inbound_frames_with_empty_payload().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM,
              http2_options.max_inbound_priority_frames_per_stream().value());
    EXPECT_EQ(OptionsLimits::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT,
              http2_options.max_inbound_window_update_frames_per_data_frame_sent().value());
  }

  {
    const std::string yaml = R"EOF(
hpack_table_size: 1
max_concurrent_streams: 2
initial_stream_window_size: 65535
initial_connection_window_size: 65535
    )EOF";
    auto http2_options = parseHttp2OptionsFromV3Yaml(yaml);
    EXPECT_EQ(1U, http2_options.hpack_table_size().value());
    EXPECT_EQ(2U, http2_options.max_concurrent_streams().value());
    EXPECT_EQ(65535U, http2_options.initial_stream_window_size().value());
    EXPECT_EQ(65535U, http2_options.initial_connection_window_size().value());
  }
}

TEST(HttpUtility, ValidateStreamErrors) {
  // Both false, the result should be false.
  envoy::config::core::v3::Http2ProtocolOptions http2_options;
  EXPECT_FALSE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options)
                   .value()
                   .override_stream_error_on_invalid_http_message()
                   .value());

  // If the new value is not present, the legacy value is respected.
  http2_options.set_stream_error_on_invalid_http_messaging(true);
  EXPECT_TRUE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // If the new value is present, it is used.
  http2_options.mutable_override_stream_error_on_invalid_http_message()->set_value(true);
  http2_options.set_stream_error_on_invalid_http_messaging(false);
  EXPECT_TRUE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // Invert values - the new value should still be used.
  http2_options.mutable_override_stream_error_on_invalid_http_message()->set_value(false);
  http2_options.set_stream_error_on_invalid_http_messaging(true);
  EXPECT_FALSE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options)
                   .value()
                   .override_stream_error_on_invalid_http_message()
                   .value());
}

TEST(HttpUtility, ValidateStreamErrorsWithHcm) {
  envoy::config::core::v3::Http2ProtocolOptions http2_options;
  http2_options.set_stream_error_on_invalid_http_messaging(true);
  EXPECT_TRUE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // If the HCM value is present it will take precedence over the old value.
  ProtobufWkt::BoolValue hcm_value;
  hcm_value.set_value(false);
  EXPECT_FALSE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options, true, hcm_value)
                   .value()
                   .override_stream_error_on_invalid_http_message()
                   .value());
  // The HCM value will be ignored if initializeAndValidateOptions is told it is not present.
  EXPECT_TRUE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options, false, hcm_value)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // The override_stream_error_on_invalid_http_message takes precedence over the
  // global one.
  http2_options.mutable_override_stream_error_on_invalid_http_message()->set_value(true);
  EXPECT_TRUE(Envoy::Http2::Utility::initializeAndValidateOptions(http2_options, true, hcm_value)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());
}

TEST(HttpUtility, ValidateStreamErrorConfigurationForHttp1) {
  envoy::config::core::v3::Http1ProtocolOptions http1_options;
  ProtobufWkt::BoolValue hcm_value;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;

  // nothing explicitly configured, default to false (i.e. default stream error behavior for HCM)
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .stream_error_on_invalid_http_message_);

  // http1_options.stream_error overrides HCM.stream_error
  http1_options.mutable_override_stream_error_on_invalid_http_message()->set_value(true);
  hcm_value.set_value(false);
  EXPECT_TRUE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                  .stream_error_on_invalid_http_message_);

  // http1_options.stream_error overrides HCM.stream_error (flip boolean value)
  http1_options.mutable_override_stream_error_on_invalid_http_message()->set_value(false);
  hcm_value.set_value(true);
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .stream_error_on_invalid_http_message_);

  http1_options.clear_override_stream_error_on_invalid_http_message();

  // fallback to HCM.stream_error
  hcm_value.set_value(true);
  EXPECT_TRUE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                  .stream_error_on_invalid_http_message_);

  // fallback to HCM.stream_error (flip boolean value)
  hcm_value.set_value(false);
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .stream_error_on_invalid_http_message_);
}

TEST(HttpUtility, UseBalsaParser) {
  envoy::config::core::v3::Http1ProtocolOptions http1_options;
  ProtobufWkt::BoolValue hcm_value;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;

  // If Http1ProtocolOptions::use_balsa_parser has no value set, then behavior is controlled by the
  // runtime flag.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "true"}});
  EXPECT_TRUE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                  .use_balsa_parser_);

  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "false"}});
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .use_balsa_parser_);

  // Enable Balsa using Http1ProtocolOptions::use_balsa_parser. Runtime flag is ignored.
  http1_options.mutable_use_balsa_parser()->set_value(true);

  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "true"}});
  EXPECT_TRUE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                  .use_balsa_parser_);

  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "false"}});
  EXPECT_TRUE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                  .use_balsa_parser_);

  // Disable Balsa using Http1ProtocolOptions::use_balsa_parser. Runtime flag is ignored.
  http1_options.mutable_use_balsa_parser()->set_value(false);

  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "true"}});
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .use_balsa_parser_);

  scoped_runtime.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "false"}});
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .use_balsa_parser_);
}

TEST(HttpUtility, AllowCustomMethods) {
  envoy::config::core::v3::Http1ProtocolOptions http1_options;
  ProtobufWkt::BoolValue hcm_value;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;

  EXPECT_FALSE(http1_options.allow_custom_methods());
  EXPECT_FALSE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                   .allow_custom_methods_);

  http1_options.set_allow_custom_methods(true);
  EXPECT_TRUE(Http1::parseHttp1Settings(http1_options, validation_visitor, hcm_value, false)
                  .allow_custom_methods_);
}

TEST(HttpUtility, getLastAddressFromXFF) {
  {
    const std::string first_address = "192.0.2.10";
    const std::string second_address = "192.0.2.1";
    const std::string third_address = "10.0.0.1";
    TestRequestHeaderMapImpl request_headers{
        {"x-forwarded-for", "192.0.2.10, 192.0.2.1, 10.0.0.1"}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(third_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
    ret = Utility::getLastAddressFromXFF(request_headers, 1);
    EXPECT_EQ(second_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
    ret = Utility::getLastAddressFromXFF(request_headers, 2);
    EXPECT_EQ(first_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
    ret = Utility::getLastAddressFromXFF(request_headers, 3);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    const std::string first_address = "192.0.2.10";
    const std::string second_address = "192.0.2.1";
    const std::string third_address = "10.0.0.1";
    const std::string fourth_address = "10.0.0.2";
    TestRequestHeaderMapImpl request_headers{
        {"x-forwarded-for", "192.0.2.10, 192.0.2.1 ,10.0.0.1,10.0.0.2"}};

    // No space on the left.
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(fourth_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);

    // No space on either side.
    ret = Utility::getLastAddressFromXFF(request_headers, 1);
    EXPECT_EQ(third_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);

    // Exercise rtrim() and ltrim().
    ret = Utility::getLastAddressFromXFF(request_headers, 2);
    EXPECT_EQ(second_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);

    // No space trimming.
    ret = Utility::getLastAddressFromXFF(request_headers, 3);
    EXPECT_EQ(first_address, ret.address_->ip()->addressAsString());
    EXPECT_FALSE(ret.allow_trusted_address_checks_);

    // No address found.
    ret = Utility::getLastAddressFromXFF(request_headers, 4);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    TestRequestHeaderMapImpl request_headers{{"x-forwarded-for", ""}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    TestRequestHeaderMapImpl request_headers{{"x-forwarded-for", ","}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    TestRequestHeaderMapImpl request_headers{{"x-forwarded-for", ", "}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    TestRequestHeaderMapImpl request_headers{{"x-forwarded-for", ", bad"}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    TestRequestHeaderMapImpl request_headers;
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(nullptr, ret.address_);
    EXPECT_FALSE(ret.allow_trusted_address_checks_);
  }
  {
    const std::string first_address = "34.0.0.1";
    TestRequestHeaderMapImpl request_headers{{"x-forwarded-for", first_address}};
    auto ret = Utility::getLastAddressFromXFF(request_headers);
    EXPECT_EQ(first_address, ret.address_->ip()->addressAsString());
    EXPECT_TRUE(ret.allow_trusted_address_checks_);
  }
}

TEST(HttpUtility, TestParseCookie) {
  TestRequestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "somekey=somevalue; someotherkey=someothervalue"},
      {"cookie", "abc=def; token=abc123; Expires=Wed, 09 Jun 2021 10:18:14 GMT"},
      {"cookie", "key2=value2; key3=value3"}};

  std::string key{"token"};
  std::string value = Utility::parseCookieValue(headers, key);
  EXPECT_EQ(value, "abc123");
}

TEST(HttpUtility, TestParseCookieDuplicates) {
  TestRequestHeaderMapImpl headers{{"someheader", "10.0.0.1"},
                                   {"cookie", "a=; b=1; a=2"},
                                   {"cookie", "a=3; b=2"},
                                   {"cookie", "b=3"}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "a"), "");
  EXPECT_EQ(Utility::parseCookieValue(headers, "b"), "1");
}

TEST(HttpUtility, TestParseSetCookie) {
  TestRequestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"set-cookie", "somekey=somevalue; someotherkey=someothervalue"},
      {"set-cookie", "abc=def; token=abc123; Expires=Wed, 09 Jun 2021 10:18:14 GMT"},
      {"set-cookie", "key2=value2; key3=value3"}};

  std::string key{"token"};
  std::string value = Utility::parseSetCookieValue(headers, key);
  EXPECT_EQ(value, "abc123");
}

TEST(HttpUtility, TestParseCookieBadValues) {
  TestRequestHeaderMapImpl headers{{"cookie", "token1=abc123; = "},
                                   {"cookie", "token2=abc123;   "},
                                   {"cookie", "; token3=abc123;"},
                                   {"cookie", "=; token4=\"abc123\""}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "token1"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token2"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token3"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token4"), "abc123");
}

TEST(HttpUtility, TestParseSetCookieBadValues) {
  TestRequestHeaderMapImpl headers{{"set-cookie", "token1=abc123; = "},
                                   {"set-cookie", "token2=abc123;   "},
                                   {"set-cookie", "; token3=abc123;"},
                                   {"set-cookie", "=; token4=\"abc123\""}};

  EXPECT_EQ(Utility::parseSetCookieValue(headers, "token1"), "abc123");
  EXPECT_EQ(Utility::parseSetCookieValue(headers, "token2"), "abc123");
  EXPECT_EQ(Utility::parseSetCookieValue(headers, "token3"), "abc123");
  EXPECT_EQ(Utility::parseSetCookieValue(headers, "token4"), "abc123");
}

TEST(HttpUtility, TestParseCookieWithQuotes) {
  TestRequestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "dquote=\"; quoteddquote=\"\"\""},
      {"cookie", "leadingdquote=\"foobar;"},
      {"cookie", "abc=def; token=\"abc123\"; Expires=Wed, 09 Jun 2021 10:18:14 GMT"}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "token"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "dquote"), "\"");
  EXPECT_EQ(Utility::parseCookieValue(headers, "quoteddquote"), "\"");
  EXPECT_EQ(Utility::parseCookieValue(headers, "leadingdquote"), "\"foobar");
}

TEST(HttpUtility, TestParseCookies) {
  TestRequestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "dquote=\"; quoteddquote=\"\"\""},
      {"cookie", "leadingdquote=\"foobar;"},
      {"cookie", "abc=def; token=\"abc123\"; Expires=Wed, 09 Jun 2021 10:18:14 GMT"}};

  const auto& cookies = Utility::parseCookies(headers);

  EXPECT_EQ(cookies.at("token"), "abc123");
  EXPECT_EQ(cookies.at("dquote"), "\"");
  EXPECT_EQ(cookies.at("quoteddquote"), "\"");
  EXPECT_EQ(cookies.at("leadingdquote"), "\"foobar");
}

TEST(HttpUtility, TestParseCookiesDuplicates) {
  TestRequestHeaderMapImpl headers{{"someheader", "10.0.0.1"},
                                   {"cookie", "a=; b=1; a=2"},
                                   {"cookie", "a=3; b=2"},
                                   {"cookie", "b=3"}};

  const auto& cookies = Utility::parseCookies(headers);

  EXPECT_EQ(cookies.at("a"), "");
  EXPECT_EQ(cookies.at("b"), "1");
}

TEST(HttpUtility, TestParseSetCookieWithQuotes) {
  TestRequestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"set-cookie", "dquote=\"; quoteddquote=\"\"\""},
      {"set-cookie", "leadingdquote=\"foobar;"},
      {"set-cookie", "abc=def; token=\"abc123\"; Expires=Wed, 09 Jun 2021 10:18:14 GMT"}};

  EXPECT_EQ(Utility::parseSetCookieValue(headers, "token"), "abc123");
  EXPECT_EQ(Utility::parseSetCookieValue(headers, "dquote"), "\"");
  EXPECT_EQ(Utility::parseSetCookieValue(headers, "quoteddquote"), "\"");
  EXPECT_EQ(Utility::parseSetCookieValue(headers, "leadingdquote"), "\"foobar");
}

TEST(HttpUtility, TestMakeSetCookieValue) {
  CookieAttributeRefVector ref_attributes;
  EXPECT_EQ("name=\"value\"; Max-Age=10",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds(10), false,
                                        ref_attributes));
  EXPECT_EQ("name=\"value\"",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds::zero(), false,
                                        ref_attributes));
  EXPECT_EQ("name=\"value\"; Max-Age=10; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds(10), true,
                                        ref_attributes));
  EXPECT_EQ("name=\"value\"; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "", std::chrono::seconds::zero(), true,
                                        ref_attributes));

  EXPECT_EQ("name=\"value\"; Max-Age=10; Path=/",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds(10), false,
                                        ref_attributes));
  EXPECT_EQ("name=\"value\"; Path=/",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds::zero(), false,
                                        ref_attributes));
  EXPECT_EQ("name=\"value\"; Max-Age=10; Path=/; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds(10), true,
                                        ref_attributes));
  EXPECT_EQ("name=\"value\"; Path=/; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds::zero(), true,
                                        ref_attributes));

  std::vector<CookieAttribute> attributes;
  attributes.push_back({"SameSite", "None"});
  attributes.push_back({"Secure", ""});
  attributes.push_back({"Partitioned", ""});
  for (const auto& attribute : attributes) {
    ref_attributes.push_back(attribute);
  }

  EXPECT_EQ("name=\"value\"; Path=/; SameSite=None; Secure; Partitioned; HttpOnly",
            Utility::makeSetCookieValue("name", "value", "/", std::chrono::seconds::zero(), true,
                                        ref_attributes));
}

TEST(HttpUtility, SendLocalReply) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, encodeHeaders_(_, false));
  EXPECT_CALL(callbacks, encodeData(_, true));
  EXPECT_CALL(callbacks, streamInfo());
  sendLocalReplyTestHelper(
      is_reset, callbacks,
      Utility::LocalReplyData{false, Http::Code::PayloadTooLarge, "large", absl::nullopt, false});
}

TEST(HttpUtility, SendLocalGrpcReply) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, streamInfo());
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), "200");
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.getGrpcStatusValue(),
                  std::to_string(enumToInt(Grpc::Status::WellKnownGrpcStatus::Unknown)));
        EXPECT_NE(headers.GrpcMessage(), nullptr);
        EXPECT_EQ(headers.getGrpcMessageValue(), "large");
      }));
  sendLocalReplyTestHelper(
      is_reset, callbacks,
      Utility::LocalReplyData{true, Http::Code::PayloadTooLarge, "large", absl::nullopt, false});
}

TEST(HttpUtility, SendLocalGrpcReplyGrpcStatusAlreadyExists) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, streamInfo());
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), "200");
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.getGrpcStatusValue(),
                  std::to_string(enumToInt(Grpc::Status::WellKnownGrpcStatus::InvalidArgument)));
        EXPECT_NE(headers.GrpcMessage(), nullptr);
        EXPECT_EQ(headers.getGrpcMessageValue(), "large");
      }));
  sendLocalReplyTestHelper(
      is_reset, callbacks,
      Utility::LocalReplyData{true, Http::Code::PayloadTooLarge, "large",
                              Grpc::Status::WellKnownGrpcStatus::InvalidArgument, false});
}

TEST(HttpUtility, SendLocalGrpcReplyGrpcStatusPreserved) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  auto encode_functions =
      Utility::EncodeFunctions{[&](ResponseHeaderMap& headers) -> void {
                                 headers.setGrpcStatus(std::to_string(
                                     enumToInt(Grpc::Status::WellKnownGrpcStatus::NotFound)));
                               },
                               nullptr,
                               [&](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
                                 callbacks.encodeHeaders(std::move(headers), end_stream, "");
                               },
                               nullptr};
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), "200");
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.getGrpcStatusValue(),
                  std::to_string(enumToInt(Grpc::Status::WellKnownGrpcStatus::NotFound)));
        EXPECT_NE(headers.GrpcMessage(), nullptr);
        EXPECT_EQ(headers.getGrpcMessageValue(), "large");
      }));
  Utility::sendLocalReply(
      is_reset, encode_functions,
      Utility::LocalReplyData{true, Http::Code::PayloadTooLarge, "large",
                              Grpc::Status::WellKnownGrpcStatus::InvalidArgument, false});
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

  EXPECT_CALL(callbacks, streamInfo());
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), "200");
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.getGrpcStatusValue(),
                  std::to_string(enumToInt(Grpc::Status::WellKnownGrpcStatus::Unauthenticated)));
        EXPECT_NE(headers.GrpcMessage(), nullptr);
        const auto& encoded = Utility::PercentEncoding::encode(json);
        EXPECT_EQ(headers.getGrpcMessageValue(), encoded);
      }));
  sendLocalReplyTestHelper(
      is_reset, callbacks,
      Utility::LocalReplyData{true, Http::Code::Unauthorized, json, absl::nullopt, false});
}

TEST(HttpUtility, RateLimitedGrpcStatus) {
  MockStreamDecoderFilterCallbacks callbacks;

  EXPECT_CALL(callbacks, streamInfo()).Times(testing::AnyNumber());
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.getGrpcStatusValue(),
                  std::to_string(enumToInt(Grpc::Status::WellKnownGrpcStatus::Unavailable)));
      }));
  sendLocalReplyTestHelper(
      false, callbacks,
      Utility::LocalReplyData{true, Http::Code::TooManyRequests, "", absl::nullopt, false});

  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(headers.GrpcStatus(), nullptr);
        EXPECT_EQ(headers.getGrpcStatusValue(),
                  std::to_string(enumToInt(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted)));
      }));
  sendLocalReplyTestHelper(
      false, callbacks,
      Utility::LocalReplyData{true, Http::Code::TooManyRequests, "",
                              absl::make_optional<Grpc::Status::GrpcStatus>(
                                  Grpc::Status::WellKnownGrpcStatus::ResourceExhausted),
                              false});
}

TEST(HttpUtility, SendLocalReplyDestroyedEarly) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;

  EXPECT_CALL(callbacks, streamInfo());
  EXPECT_CALL(callbacks, encodeHeaders_(_, false)).WillOnce(InvokeWithoutArgs([&]() -> void {
    is_reset = true;
  }));
  EXPECT_CALL(callbacks, encodeData(_, true)).Times(0);
  sendLocalReplyTestHelper(
      is_reset, callbacks,
      Utility::LocalReplyData{false, Http::Code::PayloadTooLarge, "large", absl::nullopt, false});
}

TEST(HttpUtility, SendLocalReplyHeadRequest) {
  MockStreamDecoderFilterCallbacks callbacks;
  bool is_reset = false;
  EXPECT_CALL(callbacks, streamInfo());
  EXPECT_CALL(callbacks, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getContentLengthValue(), fmt::format("{}", strlen("large")));
      }));
  sendLocalReplyTestHelper(
      is_reset, callbacks,
      Utility::LocalReplyData{false, Http::Code::PayloadTooLarge, "large", absl::nullopt, true});
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

TEST(HttpUtility, LocalPathFromFilePath) {
  EXPECT_EQ("/", Utility::localPathFromFilePath(""));
  EXPECT_EQ("c:/", Utility::localPathFromFilePath("c:/"));
  EXPECT_EQ("Z:/foo/bar", Utility::localPathFromFilePath("Z:/foo/bar"));
  EXPECT_EQ("/foo/bar", Utility::localPathFromFilePath("foo/bar"));
}

TEST(HttpUtility, TestPrepareHeaders) {
  envoy::config::core::v3::HttpUri http_uri;
  http_uri.set_uri("scheme://dns.name/x/y/z");

  Http::RequestMessagePtr message = Utility::prepareHeaders(http_uri);

  EXPECT_EQ("/x/y/z", message->headers().getPathValue());
  EXPECT_EQ("dns.name", message->headers().getHostValue());
}

TEST(HttpUtility, ResetReasonToString) {
  EXPECT_EQ("local connection failure",
            Utility::resetReasonToString(Http::StreamResetReason::LocalConnectionFailure));
  EXPECT_EQ("remote connection failure",
            Utility::resetReasonToString(Http::StreamResetReason::RemoteConnectionFailure));
  EXPECT_EQ("connection timeout",
            Utility::resetReasonToString(Http::StreamResetReason::ConnectionTimeout));
  EXPECT_EQ("connection termination",
            Utility::resetReasonToString(Http::StreamResetReason::ConnectionTermination));
  EXPECT_EQ("local reset", Utility::resetReasonToString(Http::StreamResetReason::LocalReset));
  EXPECT_EQ("local refused stream reset",
            Utility::resetReasonToString(Http::StreamResetReason::LocalRefusedStreamReset));
  EXPECT_EQ("overflow", Utility::resetReasonToString(Http::StreamResetReason::Overflow));
  EXPECT_EQ("remote reset", Utility::resetReasonToString(Http::StreamResetReason::RemoteReset));
  EXPECT_EQ("remote refused stream reset",
            Utility::resetReasonToString(Http::StreamResetReason::RemoteRefusedStreamReset));
  EXPECT_EQ("remote error with CONNECT request",
            Utility::resetReasonToString(Http::StreamResetReason::ConnectError));
  EXPECT_EQ("overload manager reset",
            Utility::resetReasonToString(Http::StreamResetReason::OverloadManager));
}

class TestConfig : public Router::RouteSpecificFilterConfig {
public:
  int state_;
  void merge(const TestConfig& other) { state_ += other.state_; }
};

// Verify that it resolveMostSpecificPerFilterConfig works with nil routes.
TEST(HttpUtility, ResolveMostSpecificPerFilterConfigNilRoute) {
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;
  filter_callbacks.route_ = nullptr;

  EXPECT_EQ(nullptr, Utility::resolveMostSpecificPerFilterConfig<TestConfig>(&filter_callbacks));
}

TEST(HttpUtility, CheckIsIpAddress) {
  std::array<std::tuple<bool, std::string, std::string, absl::optional<uint32_t>>, 15> patterns{
      std::make_tuple(true, "1.2.3.4", "1.2.3.4", absl::nullopt),
      std::make_tuple(true, "1.2.3.4:0", "1.2.3.4", 0),
      std::make_tuple(true, "0.0.0.0:4000", "0.0.0.0", 4000),
      std::make_tuple(true, "127.0.0.1:0", "127.0.0.1", 0),
      std::make_tuple(true, "[::]:0", "::", 0),
      std::make_tuple(true, "[::]", "::", absl::nullopt),
      std::make_tuple(true, "[1::2:3]:0", "1::2:3", 0),
      std::make_tuple(true, "[a::1]:0", "a::1", 0),
      std::make_tuple(true, "[a:b:c:d::]:0", "a:b:c:d::", 0),
      std::make_tuple(false, "example.com", "example.com", absl::nullopt),
      std::make_tuple(false, "example.com:8000", "example.com", 8000),
      std::make_tuple(false, "example.com:abc", "example.com:abc", absl::nullopt),
      std::make_tuple(false, "localhost:10000", "localhost", 10000),
      std::make_tuple(false, "localhost", "localhost", absl::nullopt),
      std::make_tuple(false, "", "", absl::nullopt)};

  for (const auto& pattern : patterns) {
    bool status_pattern = std::get<0>(pattern);
    const auto& try_host = std::get<1>(pattern);
    const auto& expect_host = std::get<2>(pattern);
    const auto& expect_port = std::get<3>(pattern);

    const auto host_attributes = Utility::parseAuthority(try_host);

    EXPECT_EQ(status_pattern, host_attributes.is_ip_address_);
    EXPECT_EQ(expect_host, host_attributes.host_);
    EXPECT_EQ(expect_port, host_attributes.port_);
  }
}

TEST(HttpUtility, TestConvertCoreToRouteRetryPolicy) {
  const std::string core_policy = R"(
num_retries: 10
)";

  envoy::config::core::v3::RetryPolicy core_retry_policy;
  TestUtility::loadFromYaml(core_policy, core_retry_policy);

  const envoy::config::route::v3::RetryPolicy route_retry_policy =
      Utility::convertCoreToRouteRetryPolicy(core_retry_policy,
                                             "5xx,gateway-error,connect-failure,reset");
  EXPECT_EQ(route_retry_policy.num_retries().value(), 10);
  EXPECT_EQ(route_retry_policy.per_try_timeout().seconds(), 10);
  EXPECT_EQ(route_retry_policy.retry_back_off().base_interval().seconds(), 1);
  EXPECT_EQ(route_retry_policy.retry_back_off().max_interval().seconds(), 10);
  EXPECT_EQ(route_retry_policy.retry_on(), "5xx,gateway-error,connect-failure,reset");

  const std::string core_policy2 = R"(
retry_back_off:
  base_interval: 32s
  max_interval: 1s
num_retries: 10
)";

  envoy::config::core::v3::RetryPolicy core_retry_policy2;
  TestUtility::loadFromYaml(core_policy2, core_retry_policy2);
  EXPECT_EQ(Utility::validateCoreRetryPolicy(core_retry_policy2).message(),
            "max_interval must be greater than or equal to the base_interval");
}

// Validates TE header is stripped if it contains an unsupported value
// Also validate the behavior if a nominated header does not exist
TEST(HttpUtility, TestTeHeaderGzipTrailersSanitized) {
  TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, mike, sam, will, close"},
      {"te", "gzip, trailers"},
      {"sam", "bar"},
      {"will", "baz"},
  };

  // Expect that the set of headers is valid and can be sanitized
  EXPECT_TRUE(Utility::sanitizeConnectionHeader(request_headers));

  Http::TestRequestHeaderMapImpl sanitized_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te,close"},
      {"te", "trailers"},
  };
  EXPECT_EQ(sanitized_headers, request_headers);
}

// Validates that if the connection header is nominated, the
// true connection header is not removed
TEST(HttpUtility, TestNominatedConnectionHeader) {
  TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, mike, sam, will, connection, close"},
      {"te", "gzip"},
      {"sam", "bar"},
      {"will", "baz"},
  };
  EXPECT_TRUE(Utility::sanitizeConnectionHeader(request_headers));

  TestRequestHeaderMapImpl sanitized_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "close"},
  };
  EXPECT_EQ(sanitized_headers, request_headers);
}

// Validate that if the connection header is nominated, we
// sanitize correctly preserving other nominated headers with
// supported values
TEST(HttpUtility, TestNominatedConnectionHeader2) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, mike, sam, will, connection, close"},
      {"te", "trailers"},
      {"sam", "bar"},
      {"will", "baz"},
  };
  EXPECT_TRUE(Utility::sanitizeConnectionHeader(request_headers));

  Http::TestRequestHeaderMapImpl sanitized_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te,close"},
      {"te", "trailers"},
  };
  EXPECT_EQ(sanitized_headers, request_headers);
}

// Validate that connection is rejected if pseudo headers are nominated
// This includes an extra comma to ensure that the resulting
// header is still correct
TEST(HttpUtility, TestNominatedPseudoHeader) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, :path,, :method, :authority, connection, close"},
      {"te", "trailers"},
  };

  // Headers remain unchanged since there are nominated pseudo headers
  Http::TestRequestHeaderMapImpl sanitized_headers(request_headers);

  EXPECT_FALSE(Utility::sanitizeConnectionHeader(request_headers));
  EXPECT_EQ(sanitized_headers, request_headers);
}

// Validate that we can sanitize the headers when splitting
// the Connection header results in empty tokens
TEST(HttpUtility, TestSanitizeEmptyTokensFromHeaders) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, foo,, bar, close"},
      {"te", "trailers"},
      {"foo", "monday"},
      {"bar", "friday"},
  };
  EXPECT_TRUE(Utility::sanitizeConnectionHeader(request_headers));

  Http::TestRequestHeaderMapImpl sanitized_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te,close"},
      {"te", "trailers"},
  };
  EXPECT_EQ(sanitized_headers, request_headers);
}

// Validate that we fail the request if there are too many
// nominated headers
TEST(HttpUtility, TestTooManyNominatedHeaders) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, connection, close, seahawks, niners, chargers, rams, raiders, "
                     "cardinals, eagles, giants, ravens"},
      {"te", "trailers"},
  };

  // Headers remain unchanged because there are too many nominated headers
  Http::TestRequestHeaderMapImpl sanitized_headers(request_headers);

  EXPECT_FALSE(Utility::sanitizeConnectionHeader(request_headers));
  EXPECT_EQ(sanitized_headers, request_headers);
}

TEST(HttpUtility, TestRejectNominatedXForwardedFor) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, x-forwarded-for"},
      {"te", "trailers"},
  };

  // Headers remain unchanged due to nominated X-Forwarded* header
  Http::TestRequestHeaderMapImpl sanitized_headers(request_headers);

  EXPECT_FALSE(Utility::sanitizeConnectionHeader(request_headers));
  EXPECT_EQ(sanitized_headers, request_headers);
}

TEST(HttpUtility, TestRejectNominatedXForwardedHost) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, x-forwarded-host"},
      {"te", "trailers"},
  };

  // Headers remain unchanged due to nominated X-Forwarded* header
  Http::TestRequestHeaderMapImpl sanitized_headers(request_headers);

  EXPECT_FALSE(Utility::sanitizeConnectionHeader(request_headers));
  EXPECT_EQ(sanitized_headers, request_headers);
}

TEST(HttpUtility, TestRejectNominatedForwardedProto) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, x-forwarded-proto"},
      {"te", "TrAiLeRs"},
  };

  // Headers are not sanitized due to nominated X-Forwarded* header
  EXPECT_FALSE(Utility::sanitizeConnectionHeader(request_headers));

  Http::TestRequestHeaderMapImpl sanitized_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, x-forwarded-proto"},
      {"te", "trailers"},
  };
  EXPECT_EQ(sanitized_headers, request_headers);
}

TEST(HttpUtility, TestRejectTrailersSubString) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, close"},
      {"te", "SemisWithTripleTrailersAreAthing"},
  };
  EXPECT_TRUE(Utility::sanitizeConnectionHeader(request_headers));

  Http::TestRequestHeaderMapImpl sanitized_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "close"},
  };
  EXPECT_EQ(sanitized_headers, request_headers);
}

TEST(HttpUtility, TestRejectTeHeaderTooLong) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "no-headers.com"},
      {"x-request-foo", "downstram"},
      {"connection", "te, close"},
      {"te", "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"
             "1234567890abcdef"},
  };

  // Headers remain unchanged because the TE value is too long
  Http::TestRequestHeaderMapImpl sanitized_headers(request_headers);

  EXPECT_FALSE(Utility::sanitizeConnectionHeader(request_headers));
  EXPECT_EQ(sanitized_headers, request_headers);
}

TEST(HttpUtility, TestRejectUriWithNoPath) {
  Http::TestRequestHeaderMapImpl request_headers_no_path = {
      {":method", "GET"}, {":authority", "example.com"}, {":scheme", "http"}};
  EXPECT_EQ(Utility::buildOriginalUri(request_headers_no_path, {}), "");
}

TEST(HttpUtility, TestTruncateUri) {
  Http::TestRequestHeaderMapImpl request_headers_truncated_path = {{":method", "GET"},
                                                                   {":path", "/hello_world"},
                                                                   {":authority", "example.com"},
                                                                   {":scheme", "http"}};
  EXPECT_EQ(Utility::buildOriginalUri(request_headers_truncated_path, 2), "http://example.com/h");
}

TEST(HttpUtility, TestUriUsesOriginalPath) {
  Http::TestRequestHeaderMapImpl request_headers_truncated_path = {
      {":method", "GET"},
      {":path", "/hello_world"},
      {":authority", "example.com"},
      {":scheme", "http"},
      {"x-envoy-original-path", "/goodbye_world"}};
  EXPECT_EQ(Utility::buildOriginalUri(request_headers_truncated_path, {}),
            "http://example.com/goodbye_world");
}

TEST(Url, ParsingFails) {
  Utility::Url url;
  EXPECT_FALSE(url.initialize("", false));
  EXPECT_FALSE(url.initialize("foo", false));
  EXPECT_FALSE(url.initialize("http://", false));
  EXPECT_FALSE(url.initialize("random_scheme://host.com/path", false));
  EXPECT_FALSE(url.initialize("http://www.foo.com", true));
  EXPECT_FALSE(url.initialize("foo.com", true));
  EXPECT_FALSE(url.initialize("http://[notaddress]:80/?query=param", false));
  EXPECT_FALSE(url.initialize("http://[1::z::2]:80/?query=param", false));
  EXPECT_FALSE(url.initialize("http://1.2.3.4:65536/?query=param", false));
}

void validateUrl(absl::string_view raw_url, absl::string_view expected_scheme,
                 absl::string_view expected_host_port, absl::string_view expected_path) {
  Utility::Url url;
  ASSERT_TRUE(url.initialize(raw_url, false)) << "Failed to initialize " << raw_url;
  EXPECT_EQ(url.scheme(), expected_scheme);
  EXPECT_EQ(url.hostAndPort(), expected_host_port);
  EXPECT_EQ(url.pathAndQueryParams(), expected_path);
}

void validateConnectUrl(absl::string_view raw_url) {
  Utility::Url url;
  ASSERT_TRUE(url.initialize(raw_url, true)) << "Failed to initialize " << raw_url;
  EXPECT_TRUE(url.scheme().empty());
  EXPECT_TRUE(url.pathAndQueryParams().empty());
  EXPECT_EQ(url.hostAndPort(), raw_url);
}

void invalidConnectUrl(absl::string_view raw_url) {
  Utility::Url url;
  ASSERT_FALSE(url.initialize(raw_url, true)) << "Unexpectedly initialized " << raw_url;
}

TEST(Url, ParsingTest) {
  // Test url with no explicit path (with and without port)
  validateUrl("http://www.host.com", "http", "www.host.com", "/");
  validateUrl("http://www.host.com:80", "http", "www.host.com:80", "/");

  // Test url with "/" path.
  validateUrl("http://www.host.com:80/", "http", "www.host.com:80", "/");
  validateUrl("http://www.host.com/", "http", "www.host.com", "/");

  // Test url with "?".
  validateUrl("http://www.host.com:80/?", "http", "www.host.com:80", "/?");
  validateUrl("http://www.host.com/?", "http", "www.host.com", "/?");

  // Test url with "?" but without slash.
  validateUrl("http://www.host.com:80?", "http", "www.host.com:80", "?");
  validateUrl("http://www.host.com?", "http", "www.host.com", "?");

  // Test url with multi-character path
  validateUrl("http://www.host.com:80/path", "http", "www.host.com:80", "/path");
  validateUrl("http://www.host.com/path", "http", "www.host.com", "/path");

  // Test url with multi-character path and ? at the end
  validateUrl("http://www.host.com:80/path?", "http", "www.host.com:80", "/path?");
  validateUrl("http://www.host.com/path?", "http", "www.host.com", "/path?");

  // Test https scheme
  validateUrl("https://www.host.com", "https", "www.host.com", "/");

  // Test url with query parameter
  validateUrl("http://www.host.com:80/?query=param", "http", "www.host.com:80", "/?query=param");
  validateUrl("http://www.host.com/?query=param", "http", "www.host.com", "/?query=param");

  // Test with an ipv4 host address.
  validateUrl("http://1.2.3.4/?query=param", "http", "1.2.3.4", "/?query=param");
  validateUrl("http://1.2.3.4:80/?query=param", "http", "1.2.3.4:80", "/?query=param");

  // Test with an ipv6 address
  validateUrl("http://[1::2:3]/?query=param", "http", "[1::2:3]", "/?query=param");
  validateUrl("http://[1::2:3]:80/?query=param", "http", "[1::2:3]:80", "/?query=param");

  // Test url with query parameter but without slash
  validateUrl("http://www.host.com:80?query=param", "http", "www.host.com:80", "?query=param");
  validateUrl("http://www.host.com?query=param", "http", "www.host.com", "?query=param");

  // Test url with multi-character path and query parameter
  validateUrl("http://www.host.com:80/path?query=param", "http", "www.host.com:80",
              "/path?query=param");
  validateUrl("http://www.host.com/path?query=param", "http", "www.host.com", "/path?query=param");

  // Test url with multi-character path and more than one query parameter
  validateUrl("http://www.host.com:80/path?query=param&query2=param2", "http", "www.host.com:80",
              "/path?query=param&query2=param2");
  validateUrl("http://www.host.com/path?query=param&query2=param2", "http", "www.host.com",
              "/path?query=param&query2=param2");
  // Test url with multi-character path, more than one query parameter and fragment
  validateUrl("http://www.host.com:80/path?query=param&query2=param2#fragment", "http",
              "www.host.com:80", "/path?query=param&query2=param2#fragment");
  validateUrl("http://www.host.com/path?query=param&query2=param2#fragment", "http", "www.host.com",
              "/path?query=param&query2=param2#fragment");
}

TEST(Url, ParsingForConnectTest) {
  validateConnectUrl("host.com:443");
  validateConnectUrl("host.com:80");
  validateConnectUrl("1.2.3.4:80");
  validateConnectUrl("[1:2::3:4]:80");

  invalidConnectUrl("[::12345678]:80");
  invalidConnectUrl("[1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1]:80");
  invalidConnectUrl("[1:1]:80");
  invalidConnectUrl("[:::]:80");
  invalidConnectUrl("[::1::]:80");
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

TEST(PercentEncoding, Decoding) {
  EXPECT_EQ(Utility::PercentEncoding::decode("a%26b"), "a&b");
  EXPECT_EQ(Utility::PercentEncoding::decode("hello%20world"), "hello world");
  EXPECT_EQ(Utility::PercentEncoding::decode("upstream%7Cdownstream"), "upstream|downstream");
  EXPECT_EQ(
      Utility::PercentEncoding::decode(
          "filter=%28cluster.upstream_%28rq_total%7Crq_time_sum%7Crq_time_count%7Crq_time_bucket%"
          "7Crq_xx%7Crq_complete%7Crq_active%7Ccx_active%29%29%7C%28server.version%29"),
      "filter=(cluster.upstream_(rq_total|rq_time_sum|rq_time_count|rq_time_bucket|rq_xx|rq_"
      "complete|rq_active|cx_active))|(server.version)");
}

TEST(PercentEncoding, DecodingWithTrailingInput) {
  EXPECT_EQ(Utility::PercentEncoding::decode("too%20lar%20"), "too lar ");
  EXPECT_EQ(Utility::PercentEncoding::decode("too%20larg%e"), "too larg%e");
  EXPECT_EQ(Utility::PercentEncoding::decode("too%20large%"), "too large%");
}

TEST(PercentEncoding, DecodingUrlEncodedQueryParameter) {
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("a%26b"), "a&b");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("a%3Db"), "a=b");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("a%23b"), "a#b");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("hello%20world"), "hello%20world");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("upstream%7cdownstream"),
            "upstream%7cdownstream");
  EXPECT_EQ(
      Utility::PercentEncoding::urlDecodeQueryParameter(
          "filter=%28cluster.upstream_%28rq_total%7Crq_time_sum%7Crq_time_count%7Crq_time_bucket%"
          "7Crq_xx%7Crq_complete%7Crq_active%7ccx_active%29%29%7C%28server.version%29"),
      "filter=(cluster.upstream_(rq_total%7Crq_time_sum%7Crq_time_count%7Crq_time_bucket%7Crq_xx%"
      "7Crq_"
      "complete%7Crq_active%7ccx_active))%7C(server.version)");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("too%20lar%20"), "too%20lar%20");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("too%20larg%e"), "too%20larg%e");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter("too%20large%"), "too%20large%");
  EXPECT_EQ(Utility::PercentEncoding::urlDecodeQueryParameter(
                "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
                "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"
                "%20%21%22%23%24%25%26%27%28%29%2A%2B%2C%2D%2E%2F"
                "%30%31%32%33%34%35%36%37%38%39%3A%3B%3C%3D%3E%3F"
                "%40%41%42%43%44%45%46%47%48%49%4A%4B%4C%4D%4E%4F"
                "%50%51%52%53%54%55%56%57%58%59%5A%5B%5C%5D%5E%5F"
                "%60%61%62%63%64%65%66%67%68%69%6A%6B%6C%6D%6E%6F"
                "%70%71%72%73%74%75%76%77%78%79%7A%7B%7C%7D%7E%7F"
                "%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F"
                "%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F"
                "%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF"
                "%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF"
                "%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF"
                "%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF"
                "%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF"
                "%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF"),

            "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
            "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"
            "%20!%22#$%&'()*+,-./0123456789:;%3C=%3E?"
            "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[%5C]%5E_"
            "`abcdefghijklmnopqrstuvwxyz%7B%7C%7D~%7F"
            "%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F"
            "%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F"
            "%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF"
            "%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF"
            "%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF"
            "%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF"
            "%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF"
            "%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF");
}

TEST(PercentEncoding, UrlEncodingQueryParameter) {
  EXPECT_EQ(Utility::PercentEncoding::urlEncodeQueryParameter(absl::string_view(
                "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
                "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
                "\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
                "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
                "\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4A\x4B\x4C\x4D\x4E\x4F"
                "\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5A\x5B\x5C\x5D\x5E\x5F"
                "\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6A\x6B\x6C\x6D\x6E\x6F"
                "\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7A\x7B\x7C\x7D\x7E\x7F"
                "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F"
                "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F"
                "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF"
                "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF"
                "\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF"
                "\xD0\xD1\xD2\xD3\xD4\xD5\xD6\xD7\xD8\xD9\xDA\xDB\xDC\xDD\xDE\xDF"
                "\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF"
                "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF",
                256)),

            "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
            "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"
            "%20%21%22%23%24%25%26%27%28%29*%2B%2C-.%2F"
            "0123456789%3A%3B%3C%3D%3E%3F"
            "%40ABCDEFGHIJKLMNOP"
            "QRSTUVWXYZ%5B%5C%5D%5E_"
            "%60abcdefghijklmnop"
            "qrstuvwxyz%7B%7C%7D%7E%7F"
            "%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F"
            "%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F"
            "%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF"
            "%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF"
            "%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF"
            "%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF"
            "%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF"
            "%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF");
}

TEST(PercentEncoding, Encoding) {
  EXPECT_EQ(Utility::PercentEncoding::encode("too%large"), "too%25large");
  EXPECT_EQ(Utility::PercentEncoding::encode("too%!large/"), "too%25!large/");
  EXPECT_EQ(Utility::PercentEncoding::encode("too%!large/", "%!/"), "too%25%21large%2F");
  EXPECT_EQ(Utility::PercentEncoding::encode("São Paulo"), "S%C3%A3o Paulo");
}

TEST(CheckRequiredHeaders, Request) {
  EXPECT_EQ(Http::okStatus(), HeaderUtility::checkRequiredRequestHeaders(
                                  TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/"}}));
  EXPECT_EQ(Http::okStatus(), HeaderUtility::checkRequiredRequestHeaders(TestRequestHeaderMapImpl{
                                  {":method", "CONNECT"}, {":authority", "localhost:1234"}}));
  EXPECT_EQ(absl::InvalidArgumentError("missing required header: :method"),
            HeaderUtility::checkRequiredRequestHeaders(TestRequestHeaderMapImpl{}));
  EXPECT_EQ(
      absl::InvalidArgumentError("missing required header: :path"),
      HeaderUtility::checkRequiredRequestHeaders(TestRequestHeaderMapImpl{{":method", "GET"}}));
  EXPECT_EQ(
      absl::InvalidArgumentError("missing required header: :authority"),
      HeaderUtility::checkRequiredRequestHeaders(TestRequestHeaderMapImpl{{":method", "CONNECT"}}));
}

TEST(CheckRequiredHeaders, Response) {
  EXPECT_EQ(Http::okStatus(), HeaderUtility::checkRequiredResponseHeaders(
                                  TestResponseHeaderMapImpl{{":status", "200"}}));
  EXPECT_EQ(absl::InvalidArgumentError("missing required header: :status"),
            HeaderUtility::checkRequiredResponseHeaders(TestResponseHeaderMapImpl{}));
  EXPECT_EQ(
      absl::InvalidArgumentError("missing required header: :status"),
      HeaderUtility::checkRequiredResponseHeaders(TestResponseHeaderMapImpl{{":status", "abcd"}}));
}

TEST(Utility, isSafeRequest) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};
  EXPECT_FALSE(Utility::isSafeRequest(request_headers));
  request_headers.setMethod("PUT");
  EXPECT_FALSE(Utility::isSafeRequest(request_headers));
  request_headers.setMethod("DELETE");
  EXPECT_FALSE(Utility::isSafeRequest(request_headers));
  request_headers.setMethod("PATCH");
  EXPECT_FALSE(Utility::isSafeRequest(request_headers));

  request_headers.setMethod("GET");
  EXPECT_TRUE(Utility::isSafeRequest(request_headers));
  request_headers.setMethod("HEAD");
  EXPECT_TRUE(Utility::isSafeRequest(request_headers));
  request_headers.setMethod("OPTIONS");
  EXPECT_TRUE(Utility::isSafeRequest(request_headers));
  request_headers.setMethod("TRACE");
  EXPECT_TRUE(Utility::isSafeRequest(request_headers));

  request_headers.removePath();
  request_headers.setMethod("CONNECT");
  EXPECT_FALSE(Utility::isSafeRequest(request_headers));

  request_headers.removeMethod();
  EXPECT_FALSE(Utility::isSafeRequest(request_headers));
};

TEST(Utility, isValidRefererValue) {
  EXPECT_TRUE(Utility::isValidRefererValue(absl::string_view("http://www.example.com")));
  EXPECT_TRUE(
      Utility::isValidRefererValue(absl::string_view("http://www.example.com/foo?bar=xyz")));
  EXPECT_TRUE(Utility::isValidRefererValue(absl::string_view("/resource.html")));
  EXPECT_TRUE(Utility::isValidRefererValue(absl::string_view("resource.html")));
  EXPECT_TRUE(Utility::isValidRefererValue(absl::string_view("foo/bar/resource.html")));
  EXPECT_FALSE(Utility::isValidRefererValue(absl::string_view("mal  formed/path/resource.html")));
  EXPECT_FALSE(Utility::isValidRefererValue(absl::string_view("htp:/www.malformed.com")));
  EXPECT_FALSE(
      Utility::isValidRefererValue(absl::string_view("http://www.example.com/?foo=bar#fragment")));
  EXPECT_FALSE(Utility::isValidRefererValue(absl::string_view("foo=bar#fragment")));
};
TEST(HeaderIsValidTest, SchemeIsValid) {
  EXPECT_TRUE(Utility::schemeIsValid("http"));
  EXPECT_TRUE(Utility::schemeIsValid("https"));
  EXPECT_TRUE(Utility::schemeIsValid("HtTP"));
  EXPECT_TRUE(Utility::schemeIsValid("HtTPs"));

  EXPECT_FALSE(Utility::schemeIsValid("htt"));
  EXPECT_FALSE(Utility::schemeIsValid("httpss"));
}

TEST(HeaderIsValidTest, SchemeIsHttp) {
  EXPECT_TRUE(Utility::schemeIsHttp("http"));
  EXPECT_TRUE(Utility::schemeIsHttp("htTp"));
  EXPECT_FALSE(Utility::schemeIsHttp("https"));
}

TEST(HeaderIsValidTest, SchemeIsHttps) {
  EXPECT_TRUE(Utility::schemeIsHttps("https"));
  EXPECT_TRUE(Utility::schemeIsHttps("htTps"));
  EXPECT_FALSE(Utility::schemeIsHttps("http"));
}

} // namespace Http
} // namespace Envoy
