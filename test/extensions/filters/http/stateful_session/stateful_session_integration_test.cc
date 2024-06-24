#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/stateful_session/stateful_session.h"
#include "source/extensions/http/stateful_session/cookie/cookie.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {
namespace {

class StatefulSessionIntegrationTest : public Envoy::HttpIntegrationTest, public testing::Test {
public:
  StatefulSessionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {
    // Create 4 different upstream server for stateful session test.
    setUpstreamCount(4);

    // Update endpoints of default cluster `cluster_0` to 4 different fake upstreams.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0);
      ASSERT(cluster_0->name() == "cluster_0");
      auto* endpoint = cluster_0->mutable_load_assignment()->mutable_endpoints()->Mutable(0);

      const std::string EndpointsYaml = R"EOF(
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
      )EOF";

      envoy::config::endpoint::v3::LocalityLbEndpoints new_lb_endpints;
      TestUtility::loadFromYaml(EndpointsYaml, new_lb_endpints);
      *endpoint = new_lb_endpints;
    });
  }

  // Initialize route filter and per route config.
  void initializeFilterAndRoute(const std::string& filter_yaml,
                                const std::string& per_route_config_yaml) {
    config_helper_.prependFilter(filter_yaml);

    // Create virtual host with domain `stateful.session.com` and default route to `cluster_0`
    auto virtual_host = config_helper_.createVirtualHost("stateful.session.com");

    // Update per route config of default route.
    if (!per_route_config_yaml.empty()) {
      auto* route = virtual_host.mutable_routes(0);
      ProtobufWkt::Any per_route_config;
      TestUtility::loadFromYaml(per_route_config_yaml, per_route_config);

      route->mutable_typed_per_filter_config()->insert(
          {"envoy.filters.http.stateful_session", per_route_config});
    }
    config_helper_.addVirtualHost(virtual_host);

    initialize();
  }
};

// Helper object to parse proto encoded cookie and compare with other object.
// "expire" field is ignored.
class ProtoCookieObject {
public:
  ProtoCookieObject() = delete;
  // Constructor which parses PROTO object.
  ProtoCookieObject(absl::string_view cookie) {
    std::vector<absl::string_view> v = absl::StrSplit(cookie, absl::ByChar(';'));
    std::vector<absl::string_view> sub_v = absl::StrSplit(v[0], absl::MaxSplits('=', 1));
    sub_v[1].remove_prefix(1);
    sub_v[1].remove_suffix(1);
    const std::string decoded_value = Envoy::Base64::decode(sub_v[1]);

    envoy::Cookie proto_cookie;
    if (proto_cookie.ParseFromString(decoded_value)) {
      address_ = proto_cookie.address();
    }
    max_age_ = absl::StripLeadingAsciiWhitespace(v[1]);
    path_ = absl::StripLeadingAsciiWhitespace(v[2]);
    http_ = absl::StripLeadingAsciiWhitespace(v[3]);
  }
  // Constructor which initializes individual fields
  ProtoCookieObject(std::string address, uint32_t age, std::string path, std::string http)
      : address_(address), max_age_(fmt::format("Max-Age={}", age)),
        path_(fmt::format("Path={}", path)), http_(http) {}
  bool operator==(const ProtoCookieObject& other) const {
    return (address_ == other.address_) && (max_age_ == other.max_age_) && (path_ == other.path_) &&
           (http_ == other.http_);
  }

private:
  std::string address_;
  std::string max_age_;
  std::string path_;
  std::string http_;
};

static const std::string STATEFUL_SESSION_FILTER =
    R"EOF(
name: envoy.filters.http.stateful_session
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSession
  session_state:
    name: envoy.http.stateful_session.cookie
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState
      cookie:
        name: global-session-cookie
        path: /test
        ttl: 120s
)EOF";

static const std::string STATEFUL_SESSION_HEADER_FILTER =
    R"EOF(
name: envoy.filters.http.stateful_session
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSession
  session_state:
    name: envoy.http.stateful_session.header
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.stateful_session.header.v3.HeaderBasedSessionState
      name: session-header
)EOF";
static const std::string STATEFUL_SESSION_STRICT_MODE =
    R"EOF(
  strict: true
)EOF";

static const std::string DISABLE_STATEFUL_SESSION =
    R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSessionPerRoute
disabled: true
)EOF";

static const std::string OVERRIDE_STATEFUL_SESSION =
    R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSessionPerRoute
stateful_session:
  session_state:
    name: envoy.http.stateful_session.cookie
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState
      cookie:
        name: route-session-cookie
        path: /test
        ttl: 120s
)EOF";

static const std::string OVERRIDE_STATEFUL_SESSION_HEADER =
    R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.http.stateful_session.v3.StatefulSessionPerRoute
stateful_session:
  session_state:
    name: envoy.http.stateful_session.header
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.stateful_session.header.v3.HeaderBasedSessionState
      name: route-session-header
)EOF";

TEST_F(StatefulSessionIntegrationTest, NormalStatefulSession) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, "");

  // Run the test twice. Once with proto cookie encoding and once with "old", non-proto
  // encoding.
  for (const bool use_proto : std::vector<bool>({true, false})) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                  use_proto);
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT(upstream_index.has_value());

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(upstream_index.value(), endpoint);

    std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // The selected upstream server address would be selected to the response headers.
    if (use_proto) {
      EXPECT_EQ(ProtoCookieObject(response->headers()
                                      .get(Http::LowerCaseString("set-cookie"))[0]
                                      ->value()
                                      .getStringView()),
                ProtoCookieObject(address_string, 120, "/test", "HttpOnly"));
    } else {
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());
      Http::CookieAttributeRefVector cookie_attributes;
      EXPECT_EQ(
          Envoy::Http::Utility::makeSetCookieValue("global-session-cookie", encoded_address,
                                                   "/test", std::chrono::seconds(120), true,
                                                   cookie_attributes),
          response->headers().get(Http::LowerCaseString("set-cookie"))[0]->value().getStringView());
    }
    cleanupUpstreamAndDownstream();
  }
}

TEST_F(StatefulSessionIntegrationTest, NormalStatefulSessionHeader) {
  initializeFilterAndRoute(STATEFUL_SESSION_HEADER_FILTER, "");

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "stateful.session.com"}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 0);

  auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
  ASSERT(upstream_index.has_value());

  envoy::config::endpoint::v3::LbEndpoint endpoint;
  setUpstreamAddress(upstream_index.value(), endpoint);
  const std::string address_string =
      fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
  const std::string encoded_address =
      Envoy::Base64::encode(address_string.data(), address_string.size());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  // The selected upstream server address would be selected to the response headers.
  EXPECT_EQ(
      encoded_address,
      response->headers().get(Http::LowerCaseString("session-header"))[0]->value().getStringView());

  cleanupUpstreamAndDownstream();
}

TEST_F(StatefulSessionIntegrationTest, DownstreamRequestWithStatefulSessionCookie) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, "");

  // Run the test twice. Once with proto cookie encoding and once with "old", non-proto
  // encoding.
  for (const bool use_proto : std::vector<bool>({true, false})) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                  use_proto);
    {

      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(1, endpoint);

      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string(fmt::format(
            "127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = fmt::format("127.0.0.1:{}",
                                     endpoint.endpoint().address().socket_address().port_value());
      }
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());

      codec_client_ = makeHttpConnection(lookupPort("http"));
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      // The upstream with index 1 should be selected.
      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      EXPECT_EQ(upstream_index.value(), 1);

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

      cleanupUpstreamAndDownstream();
    }

    {
      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(2, endpoint);
      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string(fmt::format(
            "127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = fmt::format("127.0.0.1:{}",
                                     endpoint.endpoint().address().socket_address().port_value());
      }
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());

      codec_client_ = makeHttpConnection(lookupPort("http"));
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      // The upstream with index 2 should be selected.
      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      EXPECT_EQ(upstream_index.value(), 2);

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

      cleanupUpstreamAndDownstream();
    }

    // Test the case that stateful session cookie with unknown server address.
    {
      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string("127.0.0.1:50000"));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = "127.0.0.1:50000";
      }
      std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());
      codec_client_ = makeHttpConnection(lookupPort("http"));
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      ASSERT(upstream_index.has_value());

      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(upstream_index.value(), endpoint);
      address_string =
          fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      if (use_proto) {
        EXPECT_EQ(ProtoCookieObject(response->headers()
                                        .get(Http::LowerCaseString("set-cookie"))[0]
                                        ->value()
                                        .getStringView()),
                  ProtoCookieObject(address_string, 120, "/test", "HttpOnly"));
      } else {
        encoded_address = Envoy::Base64::encode(address_string.data(), address_string.size());
        Http::CookieAttributeRefVector cookie_attributes;
        // The selected upstream server address would be selected to the response headers.
        EXPECT_EQ(Envoy::Http::Utility::makeSetCookieValue("global-session-cookie", encoded_address,
                                                           "/test", std::chrono::seconds(120), true,
                                                           cookie_attributes),
                  response->headers()
                      .get(Http::LowerCaseString("set-cookie"))[0]
                      ->value()
                      .getStringView());
      }

      cleanupUpstreamAndDownstream();
    }
  }
}

// Test verifies cookie-based upstream host selection with strict mode.
// When requested upstream host is valid, it should be chosen.
// When requested upstream host is invalid, Envoy should return 503.
TEST_F(StatefulSessionIntegrationTest, DownstreamRequestWithStatefulSessionCookieStrict) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER + STATEFUL_SESSION_STRICT_MODE, "");

  // When request does not contain a cookie, cluster should select endpoint using an LB and cookie
  // should be returned. In other words, it should work normally even when 'strict' mode is enabled.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    waitForNextUpstreamRequest({0, 1, 2, 3});

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // set-cookie header should be added.
    EXPECT_FALSE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

    cleanupUpstreamAndDownstream();
  }

  {

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);

    envoy::Cookie cookie;
    std::string cookie_string;
    cookie.set_address(std::string(
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
    cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count() +
                       120);
    cookie.SerializeToString(&cookie_string);

    std::string encoded_address = Envoy::Base64::encode(cookie_string.data(), cookie_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", "/test"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"},
        {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // The upstream with index 1 should be selected.
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 1);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

    cleanupUpstreamAndDownstream();
  }
  {
    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(2, endpoint);
    envoy::Cookie cookie;
    std::string cookie_string;
    cookie.set_address(std::string(
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
    cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count() +
                       120);
    cookie.SerializeToString(&cookie_string);

    std::string encoded_address = Envoy::Base64::encode(cookie_string.data(), cookie_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", "/test"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"},
        {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // The upstream with index 2 should be selected.
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 2);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

    cleanupUpstreamAndDownstream();
  }

  // Upstream endpoint encoded in the cookie points to unknown server address.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    envoy::Cookie cookie;
    std::string cookie_string;
    cookie.set_address(std::string("127.0.0.7:50000"));
    cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count() +
                       120);
    cookie.SerializeToString(&cookie_string);

    std::string encoded_address = Envoy::Base64::encode(cookie_string.data(), cookie_string.size());

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", "/test"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"},
        {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());

    EXPECT_EQ("503", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }
}

TEST_F(StatefulSessionIntegrationTest, DownstreamRequestWithStatefulSessionHeader) {
  initializeFilterAndRoute(STATEFUL_SESSION_HEADER_FILTER, "");

  {

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // The upstream with index 1 should be selected.
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 1);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

    cleanupUpstreamAndDownstream();
  }

  {
    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(2, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // The upstream with index 2 should be selected.
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 2);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

    cleanupUpstreamAndDownstream();
  }

  // Upstream endpoint encoded in stateful session header points to unknown server address.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", "/test"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"},
        {"session-header", Envoy::Base64::encode("127.0.0.7:50000", 15)}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT(upstream_index.has_value());

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(upstream_index.value(), endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // The selected upstream server address would be selected to the response headers.
    EXPECT_EQ(encoded_address, response->headers()
                                   .get(Http::LowerCaseString("session-header"))[0]
                                   ->value()
                                   .getStringView());

    cleanupUpstreamAndDownstream();
  }
}

// Test verifies header-based upstream host selection with strict mode.
// When requested upstream host is valid, it should be chosen.
// When requested upstream host is invalid, Envoy should return 503.
TEST_F(StatefulSessionIntegrationTest, DownstreamRequestWithStatefulSessionHeaderStrict) {
  initializeFilterAndRoute(STATEFUL_SESSION_HEADER_FILTER + STATEFUL_SESSION_STRICT_MODE, "");

  {

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // The upstream with index 1 should be selected.
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 1);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

    cleanupUpstreamAndDownstream();
  }
  {
    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(2, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // The upstream with index 2 should be selected.
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 2);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

    cleanupUpstreamAndDownstream();
  }

  // Upstream endpoint encoded in stateful session header points to unknown server address.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", "/test"},
        {":scheme", "http"},
        {":authority", "stateful.session.com"},
        {"session-header", Envoy::Base64::encode("127.0.0.7:50000", 15)}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());

    EXPECT_EQ("503", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }
}

TEST_F(StatefulSessionIntegrationTest, StatefulSessionDisabledByRoute) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, DISABLE_STATEFUL_SESSION);

  // Run the test twice. Once with proto cookie encoding and once with "old", non-proto
  // encoding.
  for (const bool use_proto : std::vector<bool>({true, false})) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                  use_proto);
    {
      uint64_t first_index = 0;
      uint64_t second_index = 0;

      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(1, endpoint);
      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string(fmt::format(
            "127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = fmt::format("127.0.0.1:{}",
                                     endpoint.endpoint().address().socket_address().port_value());
      }
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());

      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

      {
        codec_client_ = makeHttpConnection(lookupPort("http"));

        auto response = codec_client_->makeRequestWithBody(request_headers, 0);

        auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
        ASSERT(upstream_index.has_value());
        first_index = upstream_index.value();

        upstream_request_->encodeHeaders(default_response_headers_, true);

        ASSERT_TRUE(response->waitForEndStream());

        EXPECT_TRUE(upstream_request_->complete());
        EXPECT_TRUE(response->complete());

        // No response header to be added.
        EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

        cleanupUpstreamAndDownstream();
      }

      {
        codec_client_ = makeHttpConnection(lookupPort("http"));

        auto response = codec_client_->makeRequestWithBody(request_headers, 0);

        auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
        ASSERT(upstream_index.has_value());
        second_index = upstream_index.value();

        upstream_request_->encodeHeaders(default_response_headers_, true);

        ASSERT_TRUE(response->waitForEndStream());

        EXPECT_TRUE(upstream_request_->complete());
        EXPECT_TRUE(response->complete());

        // No response header to be added.
        EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

        cleanupUpstreamAndDownstream();
      }

      // Choose different upstream servers by default.
      EXPECT_NE(first_index, second_index);
    }
  }
  {
    uint64_t first_index = 0;
    uint64_t second_index = 0;

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      ASSERT(upstream_index.has_value());
      first_index = upstream_index.value();

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

      cleanupUpstreamAndDownstream();
    }

    {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      ASSERT(upstream_index.has_value());
      second_index = upstream_index.value();

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

      cleanupUpstreamAndDownstream();
    }

    // Choose different upstream servers by default.
    EXPECT_NE(first_index, second_index);
  }
}

TEST_F(StatefulSessionIntegrationTest, CookieStatefulSessionOverriddenByRoute) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, OVERRIDE_STATEFUL_SESSION);

  // Run the test twice. Once with proto cookie encoding and once with "old", non-proto
  // encoding.
  for (const bool use_proto : std::vector<bool>({true, false})) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                  use_proto);
    {
      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(1, endpoint);
      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string(fmt::format(
            "127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = fmt::format("127.0.0.1:{}",
                                     endpoint.endpoint().address().socket_address().port_value());
      }
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());

      codec_client_ = makeHttpConnection(lookupPort("http"));
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      ASSERT(upstream_index.has_value());

      envoy::config::endpoint::v3::LbEndpoint route_endpoint;
      setUpstreamAddress(upstream_index.value(), route_endpoint);
      const std::string route_address_string = fmt::format(
          "127.0.0.1:{}", route_endpoint.endpoint().address().socket_address().port_value());

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      if (use_proto) {
        EXPECT_EQ(ProtoCookieObject(response->headers()
                                        .get(Http::LowerCaseString("set-cookie"))[0]
                                        ->value()
                                        .getStringView()),
                  ProtoCookieObject(route_address_string, 120, "/test", "HttpOnly"));
      } else {
        const std::string route_encoded_address =
            Envoy::Base64::encode(route_address_string.data(), route_address_string.size());
        Http::CookieAttributeRefVector cookie_attributes;
        EXPECT_EQ(Envoy::Http::Utility::makeSetCookieValue(
                      "route-session-cookie", route_encoded_address, "/test",
                      std::chrono::seconds(120), true, cookie_attributes),
                  response->headers()
                      .get(Http::LowerCaseString("set-cookie"))[0]
                      ->value()
                      .getStringView());
      }

      cleanupUpstreamAndDownstream();
    }
    {
      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(2, endpoint);
      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string(fmt::format(
            "127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = fmt::format("127.0.0.1:{}",
                                     endpoint.endpoint().address().socket_address().port_value());
      }
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());

      codec_client_ = makeHttpConnection(lookupPort("http"));
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/test"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("route-session-cookie=\"{}\"", encoded_address)}};

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      // Stateful session is overridden and the upstream with index 2 should be selected..
      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      EXPECT_EQ(upstream_index.value(), 2);

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

      cleanupUpstreamAndDownstream();
    }
  }
}

TEST_F(StatefulSessionIntegrationTest, HeaderStatefulSessionOverriddenByRoute) {
  initializeFilterAndRoute(STATEFUL_SESSION_HEADER_FILTER, OVERRIDE_STATEFUL_SESSION_HEADER);

  {
    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    ASSERT(upstream_index.has_value());

    envoy::config::endpoint::v3::LbEndpoint route_endpoint;
    setUpstreamAddress(upstream_index.value(), route_endpoint);
    const std::string route_address_string = fmt::format(
        "127.0.0.1:{}", route_endpoint.endpoint().address().socket_address().port_value());
    const std::string route_encoded_address =
        Envoy::Base64::encode(route_address_string.data(), route_address_string.size());

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    EXPECT_FALSE(response->headers().get(Http::LowerCaseString("route-session-header")).empty());
    EXPECT_EQ(route_encoded_address, response->headers()
                                         .get(Http::LowerCaseString("route-session-header"))[0]
                                         ->value()
                                         .getStringView());

    cleanupUpstreamAndDownstream();
  }

  {
    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(2, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"route-session-header", encoded_address}};

    auto response = codec_client_->makeRequestWithBody(request_headers, 0);

    // Stateful session is overridden and the upstream with index 2 should be selected..
    auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
    EXPECT_EQ(upstream_index.value(), 2);

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());

    // No response header to be added.
    EXPECT_TRUE(response->headers().get(Http::LowerCaseString("route-session-header")).empty());

    cleanupUpstreamAndDownstream();
  }
}

TEST_F(StatefulSessionIntegrationTest, CookieBasedStatefulSessionDisabledByRequestPath) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, "");

  // Run the test twice. Once with proto cookie encoding and once with "old", non-proto
  // encoding.
  for (const bool use_proto : std::vector<bool>({true, false})) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                  use_proto);
    {
      uint64_t first_index = 0;
      uint64_t second_index = 0;

      envoy::config::endpoint::v3::LbEndpoint endpoint;
      setUpstreamAddress(1, endpoint);
      std::string address_string;
      if (use_proto) {
        envoy::Cookie cookie;
        cookie.set_address(std::string(fmt::format(
            "127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
        cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count() +
                           120);
        cookie.SerializeToString(&address_string);
      } else {
        address_string = fmt::format("127.0.0.1:{}",
                                     endpoint.endpoint().address().socket_address().port_value());
      }
      const std::string encoded_address =
          Envoy::Base64::encode(address_string.data(), address_string.size());

      // Request path is not start with cookie path which means that the stateful session cookie in
      // the request my not generated by current filter. The stateful session will skip processing
      // this request.
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/path_not_match"},
          {":scheme", "http"},
          {":authority", "stateful.session.com"},
          {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

      {
        codec_client_ = makeHttpConnection(lookupPort("http"));

        auto response = codec_client_->makeRequestWithBody(request_headers, 0);

        auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
        ASSERT(upstream_index.has_value());
        first_index = upstream_index.value();

        upstream_request_->encodeHeaders(default_response_headers_, true);

        ASSERT_TRUE(response->waitForEndStream());

        EXPECT_TRUE(upstream_request_->complete());
        EXPECT_TRUE(response->complete());

        // No response header to be added.
        EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

        cleanupUpstreamAndDownstream();
      }

      {
        codec_client_ = makeHttpConnection(lookupPort("http"));

        auto response = codec_client_->makeRequestWithBody(request_headers, 0);

        auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
        ASSERT(upstream_index.has_value());
        second_index = upstream_index.value();

        upstream_request_->encodeHeaders(default_response_headers_, true);

        ASSERT_TRUE(response->waitForEndStream());

        EXPECT_TRUE(upstream_request_->complete());
        EXPECT_TRUE(response->complete());

        // No response header to be added.
        EXPECT_TRUE(response->headers().get(Http::LowerCaseString("set-cookie")).empty());

        cleanupUpstreamAndDownstream();
      }

      // Choose different upstream servers by default.
      EXPECT_NE(first_index, second_index);
    }
  }
  {
    uint64_t first_index = 0;
    uint64_t second_index = 0;

    envoy::config::endpoint::v3::LbEndpoint endpoint;
    setUpstreamAddress(1, endpoint);
    const std::string address_string =
        fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());
    const std::string encoded_address =
        Envoy::Base64::encode(address_string.data(), address_string.size());

    // Request path is not start with cookie path which means that the stateful session cookie in
    // the request my not generated by current filter. The stateful session will skip processing
    // this request.
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/path_not_match"},
                                                   {":scheme", "http"},
                                                   {":authority", "stateful.session.com"},
                                                   {"session-header", encoded_address}};

    {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      ASSERT(upstream_index.has_value());
      first_index = upstream_index.value();

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

      cleanupUpstreamAndDownstream();
    }

    {
      codec_client_ = makeHttpConnection(lookupPort("http"));

      auto response = codec_client_->makeRequestWithBody(request_headers, 0);

      auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
      ASSERT(upstream_index.has_value());
      second_index = upstream_index.value();

      upstream_request_->encodeHeaders(default_response_headers_, true);

      ASSERT_TRUE(response->waitForEndStream());

      EXPECT_TRUE(upstream_request_->complete());
      EXPECT_TRUE(response->complete());

      // No response header to be added.
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("session-header")).empty());

      cleanupUpstreamAndDownstream();
    }

    // Choose different upstream servers by default.
    EXPECT_NE(first_index, second_index);
  }
}

// Test verifies that if a client sends an invalid cookie in "old", non-proto format, the
// reply is in the new proto format.
TEST_F(StatefulSessionIntegrationTest, CookieBasedStatefulSessionBackwardCompatibility) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, "");
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                true);
  std::string address_string = "127.0.0.1:50000";
  std::string encoded_address = Envoy::Base64::encode(address_string.data(), address_string.size());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test"},
      {":scheme", "http"},
      {":authority", "stateful.session.com"},
      {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 0);

  auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
  ASSERT(upstream_index.has_value());

  envoy::config::endpoint::v3::LbEndpoint endpoint;
  setUpstreamAddress(upstream_index.value(), endpoint);
  address_string =
      fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(
      ProtoCookieObject(
          response->headers().get(Http::LowerCaseString("set-cookie"))[0]->value().getStringView()),
      ProtoCookieObject(address_string, 120, "/test", "HttpOnly"));

  cleanupUpstreamAndDownstream();
}

// Test verifies that sending expired cookie results in selection of new upstream host
// and replying with a new cookie in response headers.
TEST_F(StatefulSessionIntegrationTest, CookieBasedStatefulSessionRejectExpiredCookie) {
  initializeFilterAndRoute(STATEFUL_SESSION_FILTER, "");
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.stateful_session_encode_ttl_in_cookie",
                                true);
  envoy::config::endpoint::v3::LbEndpoint endpoint;
  setUpstreamAddress(1, endpoint);
  // Create already expired cookie.
  envoy::Cookie cookie;
  cookie.set_address(std::string(
      fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value())));
  cookie.set_expires(std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count() -
                     10);
  std::string address_string;
  cookie.SerializeToString(&address_string);
  std::string encoded_address = Envoy::Base64::encode(address_string.data(), address_string.size());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test"},
      {":scheme", "http"},
      {":authority", "stateful.session.com"},
      {"cookie", fmt::format("global-session-cookie=\"{}\"", encoded_address)}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 0);

  auto upstream_index = waitForNextUpstreamRequest({0, 1, 2, 3});
  ASSERT(upstream_index.has_value());

  setUpstreamAddress(upstream_index.value(), endpoint);
  address_string =
      fmt::format("127.0.0.1:{}", endpoint.endpoint().address().socket_address().port_value());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());

  // response headers should contain a new cookie.
  EXPECT_EQ(
      ProtoCookieObject(
          response->headers().get(Http::LowerCaseString("set-cookie"))[0]->value().getStringView()),
      ProtoCookieObject(address_string, 120, "/test", "HttpOnly"));

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
