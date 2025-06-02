#include "source/extensions/http/stateful_session/envelope/envelope.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Envelope {
namespace {

TEST(EnvelopeSessionStateFactoryTest, EnvelopeSessionStateTest) {
  {
    EnvelopeSessionStateProto config;
    config.mutable_header()->set_name("session-header");

    EnvelopeSessionStateFactory factory(config);

    // No session header in the request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers;
    auto session_state = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state->upstreamAddress());

    // Empty session header in the request headers.
    request_headers.addCopy("session-header", "");
    auto session_state2 = factory.create(request_headers);
    EXPECT_EQ(absl::nullopt, session_state2->upstreamAddress());

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
              Envoy::Base64::encode("1.2.3.4:80") + ";UV:" + Envoy::Base64::encode("abcdefg"));
  }

  {
    EnvelopeSessionStateProto config;
    config.mutable_header()->set_name("session-header");

    EnvelopeSessionStateFactory factory(config);

    // Get upstream address from request headers.
    Envoy::Http::TestRequestHeaderMapImpl request_headers = {
        {":path", "/path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80")}};

    // No origin part in the request headers.
    auto session_state = factory.create(request_headers);
    EXPECT_FALSE(session_state->upstreamAddress().has_value());

    Envoy::Http::TestRequestHeaderMapImpl request_headers2 = {
        {":path", "/path"}, {"session-header", Envoy::Base64::encode("1.2.3.4:80") + ";UV:"}};

    // No valid origin part in the request headers.
    auto session_state2 = factory.create(request_headers2);
    EXPECT_FALSE(session_state2->upstreamAddress().has_value());
    EXPECT_EQ(request_headers2.get_("session-header"),
              Envoy::Base64::encode("1.2.3.4:80") + ";UV:");

    Envoy::Http::TestRequestHeaderMapImpl request_headers3 = {
        {":path", "/path"},
        {"session-header",
         Envoy::Base64::encode("1.2.3.4:80") + ";UV:" + Envoy::Base64::encode("abcdefg")}};

    auto session_state3 = factory.create(request_headers3);
    EXPECT_EQ(session_state3->upstreamAddress().value(), "1.2.3.4:80");
    EXPECT_EQ(request_headers3.get_("session-header"), "abcdefg");
  }
}

} // namespace
} // namespace Envelope
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
