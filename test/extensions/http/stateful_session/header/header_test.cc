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

TEST(HeaderBasedSessionStateFactoryTest, EmptyHeaderName) {
  HeaderBasedSessionStateProto config;
  EXPECT_THROW_WITH_MESSAGE(std::make_shared<HeaderBasedSessionStateFactory>(config),
                            EnvoyException,
                            "Header name cannot be empty for header based stateful sessions")
}

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
}

} // namespace
} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
