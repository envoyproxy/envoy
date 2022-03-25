#include "source/common/network/address_impl.h"
#include "source/extensions/http/stateful_session/cookie/cookie.h"

#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Cookie {
namespace {

TEST(CookieBasedSessionStateFactoryTest, EmptyCookieName) {
  CookieBasedSessionStateProto config;

  EXPECT_THROW_WITH_MESSAGE(std::make_shared<CookieBasedSessionStateFactory>(config),
                            EnvoyException,
                            "Cookie key cannot be empty for cookie based stateful sessions");
  config.mutable_cookie()->set_name("override_host");

  EXPECT_NO_THROW(std::make_shared<CookieBasedSessionStateFactory>(config));
}

TEST(CookieBasedSessionStateFactoryTest, SessionStateTest) {
  testing::NiceMock<Envoy::Upstream::MockHostDescription> mock_host;

  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    CookieBasedSessionStateFactory factory(config);

    // No valid address in the request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers;
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

    auto upstream_host = std::make_shared<Envoy::Network::Address::Ipv4Instance>("1.2.3.4", 80);
    EXPECT_CALL(mock_host, address()).WillOnce(testing::Return(upstream_host));

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate(mock_host, response_headers);

    // No valid address then update it by set-cookie.
    EXPECT_EQ(response_headers.get_("set-cookie"),
              Envoy::Http::Utility::makeSetCookieValue("override_host",
                                                       Envoy::Base64::encode("1.2.3.4:80", 10), "",
                                                       std::chrono::seconds(0), true));
  }

  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/path");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config);

    // Get upstream address from request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/path"}, {"cookie", "override_host=" + Envoy::Base64::encode("1.2.3.4:80", 10)}};
    auto session_state = factory.create(request_headers);
    EXPECT_EQ("1.2.3.4:80", session_state->upstreamAddress().value());

    auto upstream_host = std::make_shared<Envoy::Network::Address::Ipv4Instance>("1.2.3.4", 80);
    EXPECT_CALL(mock_host, address()).WillOnce(testing::Return(upstream_host));

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate(mock_host, response_headers);

    // Session state is not updated and then do nothing.
    EXPECT_EQ(response_headers.get_("set-cookie"), "");

    auto upstream_host_2 = std::make_shared<Envoy::Network::Address::Ipv4Instance>("2.3.4.5", 80);
    EXPECT_CALL(mock_host, address()).WillOnce(testing::Return(upstream_host_2));

    session_state->onUpdate(mock_host, response_headers);

    // Update session state because the current request is routed to a new upstream host.
    EXPECT_EQ(response_headers.get_("set-cookie"),
              Envoy::Http::Utility::makeSetCookieValue("override_host",
                                                       Envoy::Base64::encode("2.3.4.5:80", 10),
                                                       "/path", std::chrono::seconds(5), true));
  }

  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/path");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config);

    // Get upstream address from request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/not_match_path"},
        {"cookie", "override_host=" + Envoy::Base64::encode("1.2.3.4:80", 10)}};
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(nullptr, session_state);
  }
}

TEST(CookieBasedSessionStateFactoryTest, SessionStatePathMatchTest) {
  {
    // Any request path will be accepted for empty cookie path.
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config);

    EXPECT_TRUE(factory.requestPathMatch("/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo"));
    EXPECT_TRUE(factory.requestPathMatch("/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo#bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo?bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foobar"));
  }

  {
    // Any request path will be accepted for root cookie path.
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config);

    EXPECT_TRUE(factory.requestPathMatch("/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo"));
    EXPECT_TRUE(factory.requestPathMatch("/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo#bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo?bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foobar"));
  }

  {
    // Request paths that start with the cookie path will be accepted for cookie path ends with '/'.
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/foo/");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config);

    EXPECT_FALSE(factory.requestPathMatch("/"));
    EXPECT_FALSE(factory.requestPathMatch("/foo"));
    EXPECT_FALSE(factory.requestPathMatch("/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/bar"));
    EXPECT_FALSE(factory.requestPathMatch("/foo#bar"));
    EXPECT_FALSE(factory.requestPathMatch("/foo?bar"));
    EXPECT_FALSE(factory.requestPathMatch("/foobar"));
  }

  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/foo");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config);

    EXPECT_FALSE(factory.requestPathMatch("/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo"));
    EXPECT_FALSE(factory.requestPathMatch("/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo#bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo?bar"));
    EXPECT_FALSE(factory.requestPathMatch("/foobar"));
  }
}

} // namespace
} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
