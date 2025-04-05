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

TEST(HeaderBasedSessionStateFactoryTest, SessionStateTestInPackagesMode) {
  {
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_mode(HeaderBasedSessionStateProto::PACKAGES);

    HeaderBasedSessionStateFactory factory(config);

    // No valid address in the request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers;
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

    Envoy::Http::TestResponseHeaderMapImpl response_headers;
    session_state->onUpdate("1.2.3.4:80", response_headers);

    // Do nothing because no original session header in the response.
    EXPECT_EQ(response_headers.get_("session-header"), "");

    Envoy::Http::TestResponseHeaderMapImpl response_headers2{
        {":status", "200"}, {"session-header", "abcdefg"}, {"session-header", "highklm"}};
    session_state->onUpdate("1.2.3.4:80", response_headers2);

    // Do nothing because multiple session headers in the response.
    EXPECT_EQ(response_headers2.get(Envoy::Http::LowerCaseString("session-header"))[0]->value(),
              "abcdefg");
    EXPECT_EQ(response_headers2.get(Envoy::Http::LowerCaseString("session-header"))[1]->value(),
              "highklm");

    Envoy::Http::TestResponseHeaderMapImpl response_headers3{{":status", "200"},
                                                             {"session-header", "abcdefg"}};
    session_state->onUpdate("1.2.3.4:80", response_headers3);

    // Update session state because the current request is routed to a new upstream host.
    EXPECT_EQ(response_headers3.get_("session-header"),
              Envoy::Base64::encode("1.2.3.4:80;origin:abcdefg"));
  }

  {
    HeaderBasedSessionStateProto config;
    config.set_name("session-header");
    config.set_mode(HeaderBasedSessionStateProto::PACKAGES);

    HeaderBasedSessionStateFactory factory(config);

    // Get upstream address from request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80")}};

    // No origin part in the request headers.
    auto session_state = factory.create(request_headers);
    EXPECT_FALSE(session_state->upstreamAddress().has_value());

    Envoy::Http::TestRequestHeaderMapImpl request_headers2 = {
        {":path", "/path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80;origin:")}};

    // No valid origin part in the request headers.
    auto session_state2 = factory.create(request_headers2);
    EXPECT_FALSE(session_state2->upstreamAddress().has_value());

    Envoy::Http::TestRequestHeaderMapImpl request_headers3 = {
        {":path", "/path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80;origin:abcdefg")}};

    auto session_state3 = factory.create(request_headers3);
    EXPECT_EQ(session_state3->upstreamAddress().value(), "1.2.3.4:80");
    EXPECT_EQ(request_headers3.get_("session-header"), "abcdefg");
  }
}

} // namespace
} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
