#include "source/extensions/http/stateful_session/header/header.h"

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
  {
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    HeaderBasedSessionStateFactory factory(config);

    // No valid address in the request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers;
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate("1.2.3.4:80", response_headers);

    // No valid address then update it in the headers
    EXPECT_EQ(response_headers.get_("session-header"), Envoy::Base64::encode("1.2.3.4:80"));
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

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate("1.2.3.4:80", response_headers);

    // Session state is not updated so expect no header in response
    EXPECT_EQ(response_headers.get_("session-header"), "");

    session_state->onUpdate("2.3.4.5:80", response_headers);

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
