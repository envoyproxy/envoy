#include <iostream>

#include "source/common/network/address_impl.h"
#include "source/extensions/http/stateful_session/header/header.h"

#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {
namespace {

TEST(HeaderBasedSessionStateFactoryTest, SessionStateTest) {
  testing::NiceMock<Envoy::Upstream::MockHostDescription> mock_host;

  {
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    HeaderBasedSessionStateFactory factory(config);

    // No valid address in the request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers;
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

    auto upstream_host = std::make_shared<Envoy::Network::Address::Ipv4Instance>("1.2.3.4", 80);
    EXPECT_CALL(mock_host, address()).WillOnce(testing::Return(upstream_host));

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate(mock_host, response_headers);

    // No valid address then update it in the headers
    EXPECT_EQ(response_headers.get_("session-header"), Envoy::Base64::encode("1.2.3.4:80", 10));
  }

  {
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_path("/path");
    HeaderBasedSessionStateFactory factory(config);

    // Get upstream address from request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80", 10)}};

    auto session_state = factory.create(request_headers);
    EXPECT_EQ("1.2.3.4:80", session_state->upstreamAddress().value());

    auto upstream_host = std::make_shared<Envoy::Network::Address::Ipv4Instance>("1.2.3.4", 80);
    EXPECT_CALL(mock_host, address()).WillOnce(testing::Return(upstream_host));

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate(mock_host, response_headers);

    // Session state is not updated so expect no header in response
    EXPECT_EQ(response_headers.get_("session-header"), "");

    auto upstream_host_2 = std::make_shared<Envoy::Network::Address::Ipv4Instance>("2.3.4.5", 80);
    EXPECT_CALL(mock_host, address()).WillOnce(testing::Return(upstream_host_2));

    session_state->onUpdate(mock_host, response_headers);

    // Update session state because the current request is routed to a new upstream host.
    EXPECT_EQ(response_headers.get_("session-header"), Envoy::Base64::encode("2.3.4.5:80", 10));
  }

  {
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_path("/path");
    HeaderBasedSessionStateFactory factory(config);

    // Get upstream address from request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/not_match_path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80", 10)}};

    auto session_state = factory.create(request_headers);
    EXPECT_EQ(session_state, nullptr);
  }
}

TEST(HeaderBasedSessionStateFactoryTest, SessionStatePathMatchTest) {
  {
    // Any request match for empty path
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    HeaderBasedSessionStateFactory factory(config);

    EXPECT_TRUE(factory.requestPathMatch("/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo"));
    EXPECT_TRUE(factory.requestPathMatch("/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo#bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo?bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foobar"));
  }
  {
    // Any request match for root path
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_path("/");
    HeaderBasedSessionStateFactory factory(config);

    EXPECT_TRUE(factory.requestPathMatch("/"));
    EXPECT_TRUE(factory.requestPathMatch("/foo"));
    EXPECT_TRUE(factory.requestPathMatch("/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo/bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo#bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foo?bar"));
    EXPECT_TRUE(factory.requestPathMatch("/foobar"));
  }
  {
    // Request path match for config path ending with '/'
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_path("/foo/");
    HeaderBasedSessionStateFactory factory(config);

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
    // Request path match misc
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_path("/foo");
    HeaderBasedSessionStateFactory factory(config);

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
} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
