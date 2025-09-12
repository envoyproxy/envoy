#include "source/common/network/address_impl.h"
#include "source/extensions/http/stateful_session/cookie/cookie.h"

#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"
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
  Event::SimulatedTimeSystem time_simulator;

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<CookieBasedSessionStateFactory>(config, time_simulator), EnvoyException,
      "Cookie key cannot be empty for cookie based stateful sessions");
  config.mutable_cookie()->set_name("override_host");

  EXPECT_NO_THROW(std::make_shared<CookieBasedSessionStateFactory>(config, time_simulator));
}

TEST(CookieBasedSessionStateFactoryTest, SessionStateTest) {
  Event::SimulatedTimeSystem time_simulator;
  time_simulator.setMonotonicTime(std::chrono::seconds(1000));

  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    CookieBasedSessionStateFactory factory(config, time_simulator);

    // No valid address in the request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers;
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

    // No valid address then update it by set-cookie.
    std::string cookie_content;
    envoy::Cookie cookie;
    cookie.set_address("1.2.3.4:80");
    // The expiration field is not set in the cookie because TTL is 0 in the config.
    cookie.SerializeToString(&cookie_content);

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    // Check the format of the cookie sent back to client.
    session_state->onUpdate("1.2.3.4:80", response_headers);
    EXPECT_EQ(response_headers.get_("set-cookie"),
              Envoy::Http::Utility::makeSetCookieValue(
                  "override_host",
                  Envoy::Base64::encode(cookie_content.c_str(), cookie_content.length()), "",
                  std::chrono::seconds(0), true, {}));
  }

  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/path");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config, time_simulator);

    // Test the following scenario:
    // Cookie indicates to route to 1.2.3.4:80
    // Upstream cluster routed to 1.2.3.4.:80. "set-cookie" should not be added to response headers.
    // Repeat, but cluster routed to different host 2.3.4.5:80. "set-cookie" should be added to
    // response headers.

    std::string cookie_content;
    envoy::Cookie cookie;
    cookie.set_address("1.2.3.4:80");
    cookie.set_expires(1005);
    cookie.SerializeToString(&cookie_content);
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/path"},
        {"cookie", "override_host=" +
                       Envoy::Base64::encode(cookie_content.c_str(), cookie_content.length())}};
    auto session_state = factory.create(request_headers);
    EXPECT_EQ("1.2.3.4:80", session_state->upstreamAddress().value());

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate("1.2.3.4:80", response_headers);

    // Session state is not updated and then do nothing.
    EXPECT_EQ(response_headers.get_("set-cookie"), "");

    session_state->onUpdate("2.3.4.5:80", response_headers);

    // Update session state because the current request is routed to a new upstream host.
    cookie.set_address("2.3.4.5:80");
    cookie.set_expires(1005);
    cookie.SerializeToString(&cookie_content);
    EXPECT_EQ(response_headers.get_("set-cookie"),
              Envoy::Http::Utility::makeSetCookieValue(
                  "override_host",
                  Envoy::Base64::encode(cookie_content.c_str(), cookie_content.length()), "/path",
                  std::chrono::seconds(5), true, {}));
  }
  {
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->set_path("/path");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config, time_simulator);

    // Get upstream address from request headers' cookie.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/not_match_path"},
        {"cookie", "override_host=" + Envoy::Base64::encode("1.2.3.4:80", 10)}};
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(nullptr, session_state);
  }
}

TEST(CookieBasedSessionStateFactoryTest, SessionStateProtoCookie) {
  CookieBasedSessionStateProto config;
  config.mutable_cookie()->set_name("override_host");
  config.mutable_cookie()->set_path("/path");
  config.mutable_cookie()->mutable_ttl()->set_seconds(5);
  Event::SimulatedTimeSystem time_simulator;
  time_simulator.setMonotonicTime(std::chrono::seconds(1000));
  CookieBasedSessionStateFactory factory(config, time_simulator);

  std::string cookie_content;
  envoy::Cookie cookie;
  cookie.set_address("2.3.4.5:80");
  cookie.set_expires(1005);
  cookie.SerializeToString(&cookie_content);
  // PROTO format - expired cookie
  time_simulator.setMonotonicTime(std::chrono::seconds(1006));
  Envoy::Http::TestRequestHeaderMapImpl request_headers = {
      {":path", "/path"},
      {"cookie",
       "override_host=" + Envoy::Base64::encode(cookie_content.c_str(), cookie_content.length())}};
  auto session_state = factory.create(request_headers);
  EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

  // PROTO format - no "expired field"
  cookie.clear_expires();
  cookie.SerializeToString(&cookie_content);
  request_headers = {{":path", "/path"},
                     {"cookie", "override_host=" + Envoy::Base64::encode(cookie_content.c_str(),
                                                                         cookie_content.length())}};
  session_state = factory.create(request_headers);
  EXPECT_EQ("2.3.4.5:80", session_state->upstreamAddress().value());

  // PROTO format - pass incorrect format.
  // The content should be treated as "old" style encoding.
  cookie_content = "blahblah";
  request_headers = {{":path", "/path"},
                     {"cookie", "override_host=" + Envoy::Base64::encode(cookie_content.c_str(),
                                                                         cookie_content.length())}};
  session_state = factory.create(request_headers);
  EXPECT_EQ("blahblah", session_state->upstreamAddress());
}

TEST(CookieBasedSessionStateFactoryTest, SessionStatePathMatchTest) {
  Event::SimulatedTimeSystem time_simulator;
  {
    // Any request path will be accepted for empty cookie path.
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("override_host");
    config.mutable_cookie()->mutable_ttl()->set_seconds(5);
    CookieBasedSessionStateFactory factory(config, time_simulator);

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
    CookieBasedSessionStateFactory factory(config, time_simulator);

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
    CookieBasedSessionStateFactory factory(config, time_simulator);

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
    CookieBasedSessionStateFactory factory(config, time_simulator);

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

TEST(CookieBasedSessionStateFactoryTest, CookieAttributesTest) {
  Event::SimulatedTimeSystem time_simulator;
  {
    // Test cookie generation without attributes (baseline)
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("test_cookie");
    CookieBasedSessionStateFactory factory(config, time_simulator);
    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    auto session_state = factory.create(request_headers);
    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate("10.0.0.1:8080", response_headers);

    std::string actual_cookie = response_headers.get_("set-cookie");
    // Should only have HttpOnly (added by makeSetCookieValue by default)
    EXPECT_NE(actual_cookie.find("HttpOnly"), std::string::npos);
    // Should not have any custom attributes
    EXPECT_EQ(actual_cookie.find("SameSite"), std::string::npos);
    EXPECT_EQ(actual_cookie.find("Domain"), std::string::npos);
  }
  {
    // Test cookie with multiple attributes
    CookieBasedSessionStateProto config;
    config.mutable_cookie()->set_name("multi_attr_cookie");
    // Add SameSite attribute
    auto* attr1 = config.mutable_cookie()->add_attributes();
    attr1->set_name("SameSite");
    attr1->set_value("Lax");
    // Add Secure attribute (boolean - empty value)
    auto* attr2 = config.mutable_cookie()->add_attributes();
    attr2->set_name("Secure");
    attr2->set_value("");
    // Add Domain attribute
    auto* attr3 = config.mutable_cookie()->add_attributes();
    attr3->set_name("Domain");
    attr3->set_value("example.com");
    CookieBasedSessionStateFactory factory(config, time_simulator);
    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    auto session_state = factory.create(request_headers);
    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate("10.1.1.1:443", response_headers);

    std::string actual_cookie = response_headers.get_("set-cookie");
    // Should have HttpOnly (added by makeSetCookieValue by default)
    EXPECT_NE(actual_cookie.find("HttpOnly"), std::string::npos);
    // Should also have custom attributes
    EXPECT_NE(actual_cookie.find("SameSite=Lax"), std::string::npos);
    EXPECT_NE(actual_cookie.find("Secure"), std::string::npos);
    EXPECT_NE(actual_cookie.find("Domain=example.com"), std::string::npos);
  }
}

} // namespace
} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
