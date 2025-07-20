#include "test/mocks/http/mocks.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "contrib/mcp_sse_stateful_session/filters/http/source/mcp_sse.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace McpSse {

class McpSseSessionStateTest : public testing::Test {
protected:
  void SetUp() override {
    config_.set_param_name("sessionId");
    factory_ = std::make_unique<McpSseSessionStateFactoryImpl>(config_);
  }

  envoy::extensions::http::stateful_session::mcp_sse::v3::McpSseSessionState config_;
  std::unique_ptr<McpSseSessionStateFactoryImpl> factory_;
};

TEST_F(McpSseSessionStateTest, ParseAddressWithValidSession) {
  NiceMock<Envoy::Http::TestRequestHeaderMapImpl> headers;
  headers.setPath("/path?sessionId=abc123.MTI3LjAuMC4xOjgw&other=value");

  auto session_state = factory_->create(headers);
  ASSERT_NE(session_state, nullptr);

  auto address = session_state->upstreamAddress();
  ASSERT_TRUE(address.has_value());
  EXPECT_EQ(address.value(), "127.0.0.1:80");

  // Check that the path was updated to remove the encoded host
  EXPECT_EQ(headers.getPathValue(), "/path?sessionId=abc123&other=value");
}

TEST_F(McpSseSessionStateTest, ParseAddressWithNoSession) {
  NiceMock<Envoy::Http::TestRequestHeaderMapImpl> headers;
  headers.setPath("/path?other=value");

  auto session_state = factory_->create(headers);
  ASSERT_NE(session_state, nullptr);

  auto address = session_state->upstreamAddress();
  EXPECT_FALSE(address.has_value());
}

TEST_F(McpSseSessionStateTest, ParseAddressWithNoSeparator) {
  NiceMock<Envoy::Http::TestRequestHeaderMapImpl> headers;
  headers.setPath("/path?sessionId=abc123&other=value");

  auto session_state = factory_->create(headers);
  ASSERT_NE(session_state, nullptr);

  auto address = session_state->upstreamAddress();
  EXPECT_FALSE(address.has_value());
}

TEST_F(McpSseSessionStateTest, ParseAddressWithInvalidBase64) {
  NiceMock<Envoy::Http::TestRequestHeaderMapImpl> headers;
  headers.setPath("/path?sessionId=abc123.==Zg&other=value");

  auto session_state = factory_->create(headers);
  ASSERT_NE(session_state, nullptr);

  auto address = session_state->upstreamAddress();
  EXPECT_FALSE(address.has_value());
}

TEST_F(McpSseSessionStateTest, UpdateDataWithSSEResponse) {
  NiceMock<Envoy::Http::TestRequestHeaderMapImpl> request_headers;
  auto session_state = factory_->create(request_headers);
  ASSERT_NE(session_state, nullptr);

  NiceMock<Envoy::Http::TestResponseHeaderMapImpl> response_headers;
  response_headers.setContentType("text/event-stream");

  // Test SSE data processing
  Buffer::OwnedImpl data;
  data.add("data: {\"url\": \"/path?sessionId=abc123&other=value\"}\r\n\r\n");

  session_state->onUpdateHeader("127.0.0.1:80", response_headers);
  auto status = session_state->onUpdateData("127.0.0.1:80", data, false);

  EXPECT_EQ(status, Envoy::Http::FilterDataStatus::Continue);

  // Check that the session ID was encoded with the host
  std::string result = data.toString();
  EXPECT_THAT(result, testing::HasSubstr("sessionId=abc123.MTI3LjAuMC4xOjgw"));
}

TEST_F(McpSseSessionStateTest, UpdateDataWithNonSSEResponse) {
  NiceMock<Envoy::Http::TestRequestHeaderMapImpl> request_headers;
  auto session_state = factory_->create(request_headers);
  ASSERT_NE(session_state, nullptr);

  NiceMock<Envoy::Http::TestResponseHeaderMapImpl> response_headers;
  response_headers.setContentType("application/json");

  Buffer::OwnedImpl data;
  data.add("{\"data\": \"test\"}");

  session_state->onUpdateHeader("127.0.0.1:80", response_headers);
  auto status = session_state->onUpdateData("127.0.0.1:80", data, false);

  EXPECT_EQ(status, Envoy::Http::FilterDataStatus::Continue);
  EXPECT_EQ(data.toString(), "{\"data\": \"test\"}");
}

} // namespace McpSse
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
