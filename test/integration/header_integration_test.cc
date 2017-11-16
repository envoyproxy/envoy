#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {

class HeaderIntegrationTest : public HttpIntegrationTest,
                              public testing::TestWithParam<Network::Address::IpVersion> {
public:
  HeaderIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  enum HeaderMode {
    Append = 1,
    Replace = 2,
    AppendWithRouteConfigHeaders = 3,
    ReplaceWithRouteConfigHeaders = 4
  };

  void init(HeaderMode mode) {
    BaseIntegrationTest::initialize();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());

    std::string route_config_header_fmt = R"EOF(
      {{
        "header": {{ "key": "x-routeconfig-{}", "value": "routeconfig" }},
        "append": {}
      }})EOF";

    std::string route_config_response_headers_to_add, route_config_response_headers_to_remove,
        route_config_request_headers_to_add;
    bool append = false;

    switch (mode) {
    case HeaderMode::ReplaceWithRouteConfigHeaders:
      route_config_response_headers_to_add =
          fmt::format(route_config_header_fmt, "response", "false");
      route_config_response_headers_to_remove = R"EOF("x-routeconfig-response-remove")EOF";
      route_config_request_headers_to_add =
          fmt::format(route_config_header_fmt, "request", "false");
      append = false;
      break;

    case HeaderMode::Replace:
      append = false;
      break;

    case HeaderMode::AppendWithRouteConfigHeaders:
      route_config_response_headers_to_add =
          fmt::format(route_config_header_fmt, "response", "true");
      route_config_response_headers_to_remove = R"EOF("x-routeconfig-response-remove")EOF";
      route_config_request_headers_to_add = fmt::format(route_config_header_fmt, "request", "true");
      append = true;
      break;

    case HeaderMode::Append:
      append = true;
      break;

    default:
      ASSERT(false);
    }

    registerParam("append", append ? "true" : "false");
    registerParam("route_config_response_headers_to_add", route_config_response_headers_to_add);
    registerParam("route_config_response_headers_to_remove",
                  route_config_response_headers_to_remove);
    registerParam("route_config_request_headers_to_add", route_config_request_headers_to_add);

    createTestServer("test/config/integration/server_header.json", {"http"});
  }

protected:
  void performRequest(Http::TestHeaderMapImpl&& request_headers,
                      Http::TestHeaderMapImpl&& expected_request_headers,
                      Http::TestHeaderMapImpl&& response_headers,
                      Http::TestHeaderMapImpl&& expected_response_headers) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

    EXPECT_TRUE(response_->complete());

    compareHeaders(upstream_request_->headers(), expected_request_headers);
    compareHeaders(response_->headers(), expected_response_headers);
  }

  void compareHeaders(Http::TestHeaderMapImpl&& headers,
                      Http::TestHeaderMapImpl& expected_headers) {
    headers.remove(Envoy::Http::LowerCaseString{"content-length"});
    headers.remove(Envoy::Http::LowerCaseString{"date"});
    headers.remove(Envoy::Http::LowerCaseString{"x-envoy-expected-rq-timeout-ms"});
    headers.remove(Envoy::Http::LowerCaseString{"x-envoy-upstream-service-time"});
    headers.remove(Envoy::Http::LowerCaseString{"x-forwarded-proto"});
    headers.remove(Envoy::Http::LowerCaseString{"x-request-id"});

    if (!(expected_headers == headers)) {
      std::cout << "got: " << std::endl;
      headers.iterate(
          [](const Http::HeaderEntry& header, void*) -> Http::HeaderMap::Iterate {
            std::cout << "  " << header.key().c_str() << ": " << header.value().c_str()
                      << std::endl;
            return Http::HeaderMap::Iterate::Continue;
          },
          nullptr);

      std::cout << "expected: " << std::endl;
      expected_headers.iterate(
          [](const Http::HeaderEntry& header, void*) -> Http::HeaderMap::Iterate {
            std::cout << "  " << header.key().c_str() << ": " << header.value().c_str()
                      << std::endl;
            return Http::HeaderMap::Iterate::Continue;
          },
          nullptr);
    }

    EXPECT_EQ(expected_headers, headers);
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, HeaderIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Validate that downstream request headers are passed upstream and upstream response headers are
// passed downstream.
TEST_P(HeaderIntegrationTest, TestRequestAndResponseHeaderPassThrough) {
  init(HeaderMode::Append);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/"},
          {":scheme", "http"},
          {":authority", "no-headers.com"},
          {"x-request-foo", "downstram"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "no-headers.com"},
          {"x-request-foo", "downstram"},
          {":path", "/"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-return-foo", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-return-foo", "upstream"},
          {":status", "200"},
      });
}

// Validates the virtual host appends upstream request headers and appends/removes upstream
// response headers.
TEST_P(HeaderIntegrationTest, TestVirtualHostAppendHeaderManipulation) {
  init(HeaderMode::Append);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-only"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-only"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the virtual host replaces request headers and replaces upstream response headers.
TEST_P(HeaderIntegrationTest, TestVirtualHostReplaceHeaderManipulation) {
  init(HeaderMode::Replace);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-only"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-unmodified", "downstream"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "downstream"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-only"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-unmodified", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "upstream"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the route appends request headers and appends/removes upstream response headers.
TEST_P(HeaderIntegrationTest, TestRouteAppendHeaderManipulation) {
  init(HeaderMode::Append);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/route-only"},
          {":scheme", "http"},
          {":authority", "route-headers.com"},
          {"x-route-request", "downstream"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "route-headers.com"},
          {"x-route-request", "downstream"},
          {"x-route-request", "route"},
          {":path", "/route-only"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-route-response", "upstream"},
          {"x-route-response", "route"},
          {":status", "200"},
      });
}

// Validates the route replaces request headers and replaces/removes upstream response headers.
TEST_P(HeaderIntegrationTest, TestRouteReplaceHeaderManipulation) {
  init(HeaderMode::Replace);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/route-only"},
          {":scheme", "http"},
          {":authority", "route-headers.com"},
          {"x-route-request", "downstream"},
          {"x-unmodified", "downstream"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "route-headers.com"},
          {"x-unmodified", "downstream"},
          {"x-route-request", "route"},
          {":path", "/route-only"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
          {"x-unmodified", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "upstream"},
          {"x-route-response", "route"},
          {":status", "200"},
      });
}

// Validates the relationship between virtual host and route header manipulations when appending.
TEST_P(HeaderIntegrationTest, TestVirtualHostAndRouteAppendHeaderManipulation) {
  init(HeaderMode::Append);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the relationship between virtual host and route header manipulations when replacing.
TEST_P(HeaderIntegrationTest, TestVirtualHostAndRouteReplaceHeaderManipulation) {
  init(HeaderMode::Replace);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-unmodified", "request"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "request"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-unmodified", "response"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {":status", "200"},
      });
}

// Validates the relationship between route configuration, virtual host and route header
// manipulations when appending.
TEST_P(HeaderIntegrationTest, TestRouteConfigVirtualHostAndRouteAppendHeaderManipulation) {
  init(HeaderMode::AppendWithRouteConfigHeaders);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-routeconfig-response-remove", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-vhost-response-remove", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response-remove", "upstream"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {"x-routeconfig-response", "routeconfig"},
          {":status", "200"},
      });
}

// Validates the relationship between route configuration, virtual host and route header
// manipulations when replacing.
TEST_P(HeaderIntegrationTest, TestRouteConfigVirtualHostAndRouteReplaceHeaderManipulation) {
  init(HeaderMode::ReplaceWithRouteConfigHeaders);
  performRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/vhost-and-route"},
          {":scheme", "http"},
          {":authority", "vhost-headers.com"},
          {"x-routeconfig-request", "downstream"},
          {"x-vhost-request", "downstream"},
          {"x-route-request", "downstream"},
          {"x-unmodified", "request"},
      },
      Http::TestHeaderMapImpl{
          {":authority", "vhost-headers.com"},
          {"x-unmodified", "request"},
          {"x-route-request", "route"},
          {"x-vhost-request", "vhost"},
          {"x-routeconfig-request", "routeconfig"},
          {":path", "/vhost-and-route"},
          {":method", "GET"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
          {"x-routeconfig-response", "upstream"},
          {"x-vhost-response", "upstream"},
          {"x-route-response", "upstream"},
          {"x-unmodified", "response"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"x-unmodified", "response"},
          {"x-route-response", "route"},
          {"x-vhost-response", "vhost"},
          {"x-routeconfig-response", "routeconfig"},
          {":status", "200"},
      });
}
} // namespace Envoy
