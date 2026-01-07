#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using testing::NiceMock;

// Verifies parseMethodString correctly maps MCP method strings to enum values.
TEST(ParseMethodStringTest, AllMethods) {
  EXPECT_EQ(parseMethodString("initialize"), McpMethod::Initialize);
  EXPECT_EQ(parseMethodString("tools/list"), McpMethod::ToolsList);
  EXPECT_EQ(parseMethodString("tools/call"), McpMethod::ToolsCall);
  EXPECT_EQ(parseMethodString("ping"), McpMethod::Ping);
  EXPECT_EQ(parseMethodString("notifications/initialized"), McpMethod::NotificationInitialized);
  EXPECT_EQ(parseMethodString("unknown_method"), McpMethod::Unknown);
  EXPECT_EQ(parseMethodString(""), McpMethod::Unknown);
}

class McpRouterConfigTest : public testing::Test {
protected:
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
};

// Verifies multiple backends enable multiplexing mode and findBackend works.
TEST_F(McpRouterConfigTest, MultipleBackendsEnablesMultiplexing) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;

  auto* server1 = proto_config.add_servers();
  server1->set_name("time");
  server1->mutable_mcp_cluster()->set_cluster("time_cluster");
  server1->mutable_mcp_cluster()->set_path("/mcp/time");

  auto* server2 = proto_config.add_servers();
  server2->set_name("calc");
  server2->mutable_mcp_cluster()->set_cluster("calc_cluster");
  server2->mutable_mcp_cluster()->set_path("/mcp/calc");

  McpRouterConfig config(proto_config, factory_context_);

  EXPECT_EQ(config.backends().size(), 2);
  EXPECT_TRUE(config.isMultiplexing());
  EXPECT_TRUE(config.defaultBackendName().empty());

  const McpBackendConfig* time_backend = config.findBackend("time");
  ASSERT_NE(time_backend, nullptr);
  EXPECT_EQ(time_backend->name, "time");
  EXPECT_EQ(time_backend->cluster_name, "time_cluster");
  EXPECT_EQ(time_backend->path, "/mcp/time");

  const McpBackendConfig* calc_backend = config.findBackend("calc");
  ASSERT_NE(calc_backend, nullptr);
  EXPECT_EQ(calc_backend->name, "calc");

  EXPECT_EQ(config.findBackend("nonexistent"), nullptr);
}

// Verifies single backend sets default backend name and disables multiplexing.
TEST_F(McpRouterConfigTest, SingleBackendSetsDefaultName) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;

  auto* server = proto_config.add_servers();
  server->set_name("tools");
  server->mutable_mcp_cluster()->set_cluster("tools_cluster");

  McpRouterConfig config(proto_config, factory_context_);

  EXPECT_EQ(config.backends().size(), 1);
  EXPECT_FALSE(config.isMultiplexing());
  EXPECT_EQ(config.defaultBackendName(), "tools");
}

// Verifies backend path defaults to "/mcp" when not specified.
TEST_F(McpRouterConfigTest, DefaultPathWhenNotSpecified) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;

  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  McpRouterConfig config(proto_config, factory_context_);

  const McpBackendConfig* backend = config.findBackend("test");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->path, "/mcp");
}

class BackendStreamCallbacksTest : public testing::Test {};

// Verifies successful response correctly populates BackendResponse fields.
TEST_F(BackendStreamCallbacksTest, SuccessfulResponse) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("test_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->addCopy(Http::LowerCaseString("mcp-session-id"), "session-123");
  callbacks->onHeaders(std::move(headers), false);

  Buffer::OwnedImpl data("{\"result\":\"ok\"}");
  callbacks->onData(data, true);

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_response.backend_name, "test_backend");
  EXPECT_TRUE(received_response.success);
  EXPECT_EQ(received_response.status_code, 200);
  EXPECT_EQ(received_response.body, "{\"result\":\"ok\"}");
  EXPECT_EQ(received_response.session_id, "session-123");
}

// Verifies stream reset marks response as failure with error message.
TEST_F(BackendStreamCallbacksTest, StreamResetMarksFailure) {
  BackendResponse received_response;
  bool callback_invoked = false;

  auto callbacks =
      std::make_shared<BackendStreamCallbacks>("test_backend", [&](BackendResponse resp) {
        callback_invoked = true;
        received_response = std::move(resp);
      });

  callbacks->onReset();

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(received_response.backend_name, "test_backend");
  EXPECT_FALSE(received_response.success);
  EXPECT_EQ(received_response.error, "Stream reset");
}

// Verifies callback is invoked exactly once even with multiple completion signals.
TEST_F(BackendStreamCallbacksTest, CallbackInvokedOnlyOnce) {
  int callback_count = 0;

  auto callbacks = std::make_shared<BackendStreamCallbacks>(
      "test_backend", [&](BackendResponse) { callback_count++; });

  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  callbacks->onHeaders(std::move(headers), false);

  Buffer::OwnedImpl data("data");
  callbacks->onData(data, true);

  callbacks->onComplete();
  callbacks->onReset();

  EXPECT_EQ(callback_count, 1);
}

class SessionCodecTest : public testing::Test {};

// Verifies encode/decode round-trip preserves data.
TEST_F(SessionCodecTest, EncodeDecodeRoundTrip) {
  std::string data = "hello world";

  std::string encoded = SessionCodec::encode(data);
  EXPECT_NE(encoded, data);

  std::string decoded = SessionCodec::decode(encoded);
  EXPECT_EQ(decoded, data);
}

// Verifies encode/decode handles empty strings.
TEST_F(SessionCodecTest, EncodeDecodeEmptyString) {
  std::string encoded = SessionCodec::encode("");
  std::string decoded = SessionCodec::decode(encoded);
  EXPECT_EQ(decoded, "");
}

// Verifies composite session ID contains route, subject, and backend info.
TEST_F(SessionCodecTest, BuildCompositeSessionId) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"backend1", "session-abc"},
      {"backend2", "session-xyz"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("route1", "user1", sessions);

  EXPECT_TRUE(absl::StrContains(composite, "route1@"));
  EXPECT_TRUE(absl::StrContains(composite, "@user1@"));
  EXPECT_TRUE(absl::StrContains(composite, "backend1:"));
  EXPECT_TRUE(absl::StrContains(composite, "backend2:"));
}

// Verifies parsing correctly extracts route, subject, and backend sessions.
TEST_F(SessionCodecTest, ParseCompositeSessionId) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"time", "sess-time"},
      {"tools", "sess-tools"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("myroute", "myuser", sessions);

  auto parsed = SessionCodec::parseCompositeSessionId(composite);
  ASSERT_TRUE(parsed.ok());

  EXPECT_EQ(parsed->route, "myroute");
  EXPECT_EQ(parsed->subject, "myuser");
  EXPECT_EQ(parsed->backend_sessions.size(), 2);
  EXPECT_EQ(parsed->backend_sessions["time"], "sess-time");
  EXPECT_EQ(parsed->backend_sessions["tools"], "sess-tools");
}

// Verifies parsing rejects malformed session IDs.
TEST_F(SessionCodecTest, ParseCompositeSessionIdRejectsMalformedInput) {
  EXPECT_FALSE(SessionCodec::parseCompositeSessionId("no-at-signs").ok());
  EXPECT_FALSE(SessionCodec::parseCompositeSessionId("one@part").ok());
  EXPECT_FALSE(SessionCodec::parseCompositeSessionId("route@user@backend-no-colon").ok());
  EXPECT_FALSE(SessionCodec::parseCompositeSessionId("route@user@:session").ok());
}

// Verifies full encode-decode-parse round-trip.
TEST_F(SessionCodecTest, FullRoundTrip) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"backend1", "session-123"},
      {"backend2", "session-456"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("route", "subject", sessions);
  std::string encoded = SessionCodec::encode(composite);
  std::string decoded = SessionCodec::decode(encoded);

  auto parsed = SessionCodec::parseCompositeSessionId(decoded);
  ASSERT_TRUE(parsed.ok());

  EXPECT_EQ(parsed->route, "route");
  EXPECT_EQ(parsed->subject, "subject");
  EXPECT_EQ(parsed->backend_sessions["backend1"], "session-123");
  EXPECT_EQ(parsed->backend_sessions["backend2"], "session-456");
}

// Verifies special characters in session IDs are handled correctly.
TEST_F(SessionCodecTest, SpecialCharactersInSessionId) {
  absl::flat_hash_map<std::string, std::string> sessions = {
      {"backend", "sess+with/special=chars"},
  };

  std::string composite = SessionCodec::buildCompositeSessionId("route", "user", sessions);
  std::string encoded = SessionCodec::encode(composite);
  std::string decoded = SessionCodec::decode(encoded);

  auto parsed = SessionCodec::parseCompositeSessionId(decoded);
  ASSERT_TRUE(parsed.ok());

  EXPECT_EQ(parsed->backend_sessions["backend"], "sess+with/special=chars");
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
