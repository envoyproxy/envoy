#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using testing::AnyNumber;
using testing::NiceMock;
using testing::ReturnRef;

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

// Verifies session identity config is disabled by default.
TEST_F(McpRouterConfigTest, SessionIdentityDisabledByDefault) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  McpRouterConfig config(proto_config, factory_context_);
  EXPECT_FALSE(config.hasSessionIdentity());
  EXPECT_FALSE(config.shouldEnforceValidation());
}

// Verifies session identity config with header source.
TEST_F(McpRouterConfigTest, SessionIdentityWithHeaderSource) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  identity->mutable_identity()->mutable_header()->set_name("x-user-id");

  McpRouterConfig config(proto_config, factory_context_);
  EXPECT_TRUE(config.hasSessionIdentity());
  EXPECT_TRUE(absl::holds_alternative<HeaderSubjectSource>(config.subjectSource()));
  EXPECT_FALSE(config.shouldEnforceValidation()); // DISABLED by default
}

// Verifies session identity config with metadata source using MetadataKey.
TEST_F(McpRouterConfigTest, SessionIdentityWithMetadataSource) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  auto* metadata_key = identity->mutable_identity()->mutable_dynamic_metadata()->mutable_key();
  metadata_key->set_key("envoy.filters.http.jwt_authn");
  metadata_key->add_path()->set_key("payload");
  metadata_key->add_path()->set_key("sub");

  McpRouterConfig config(proto_config, factory_context_);
  EXPECT_TRUE(config.hasSessionIdentity());
  EXPECT_TRUE(absl::holds_alternative<MetadataSubjectSource>(config.subjectSource()));
}

// Verifies metadata key path is parsed correctly.
TEST_F(McpRouterConfigTest, MetadataKeyPathParsed) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  auto* metadata_key = identity->mutable_identity()->mutable_dynamic_metadata()->mutable_key();
  metadata_key->set_key("jwt");
  metadata_key->add_path()->set_key("payload");
  metadata_key->add_path()->set_key("sub");

  McpRouterConfig config(proto_config, factory_context_);
  const auto& source = absl::get<MetadataSubjectSource>(config.subjectSource());
  EXPECT_EQ(source.filter, "jwt");
  ASSERT_EQ(source.path_keys.size(), 2);
  EXPECT_EQ(source.path_keys[0], "payload");
  EXPECT_EQ(source.path_keys[1], "sub");
}

// Verifies validation mode ENFORCE is parsed correctly.
TEST_F(McpRouterConfigTest, ValidationModeEnforce) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  identity->mutable_identity()->mutable_header()->set_name("x-user-id");
  identity->mutable_validation()->set_mode(
      envoy::extensions::filters::http::mcp_router::v3::ValidationPolicy::ENFORCE);

  McpRouterConfig config(proto_config, factory_context_);
  EXPECT_TRUE(config.hasSessionIdentity());
  EXPECT_TRUE(config.shouldEnforceValidation());
  EXPECT_EQ(config.validationMode(), ValidationMode::Enforce);
}

// Test fixture for McpRouterFilter runtime behavior tests.
class McpRouterFilterTest : public testing::Test {
protected:
  void SetUp() override {
    EXPECT_CALL(decoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(dynamic_metadata_));
  }

  McpRouterConfigSharedPtr
  createConfig(const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config) {
    return std::make_shared<McpRouterConfig>(proto_config, factory_context_);
  }

  void setDynamicMetadata(const std::string& filter_name, const std::string& key,
                          const std::string& value) {
    auto& filter_metadata = (*dynamic_metadata_.mutable_filter_metadata())[filter_name];
    (*filter_metadata.mutable_fields())[key].set_string_value(value);
  }

  void setNestedDynamicMetadata(const std::string& filter_name,
                                const std::vector<std::string>& path, const std::string& value) {
    auto& filter_metadata = (*dynamic_metadata_.mutable_filter_metadata())[filter_name];
    Protobuf::Struct* current = &filter_metadata;

    for (size_t i = 0; i < path.size() - 1; ++i) {
      current = (*current->mutable_fields())[path[i]].mutable_struct_value();
    }
    (*current->mutable_fields())[path.back()].set_string_value(value);
  }

  void setNestedDynamicMetadataNumber(const std::string& filter_name,
                                      const std::vector<std::string>& path, double value) {
    auto& filter_metadata = (*dynamic_metadata_.mutable_filter_metadata())[filter_name];
    Protobuf::Struct* current = &filter_metadata;

    for (size_t i = 0; i < path.size() - 1; ++i) {
      current = (*current->mutable_fields())[path[i]].mutable_struct_value();
    }
    (*current->mutable_fields())[path.back()].set_number_value(value);
  }

  void setMcpMethodMetadata(const std::string& method, int64_t id = 1) {
    auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["mcp_proxy"];
    (*mcp_metadata.mutable_fields())["method"].set_string_value(method);
    (*mcp_metadata.mutable_fields())["id"].set_number_value(static_cast<double>(id));
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
};

// Verifies subject extraction from dynamic metadata succeeds.
TEST_F(McpRouterFilterTest, MetadataSubjectExtractionSuccess) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  auto* metadata_key = identity->mutable_identity()->mutable_dynamic_metadata()->mutable_key();
  metadata_key->set_key("envoy.filters.http.jwt_authn");
  metadata_key->add_path()->set_key("payload");
  metadata_key->add_path()->set_key("sub");
  identity->mutable_validation()->set_mode(
      envoy::extensions::filters::http::mcp_router::v3::ValidationPolicy::ENFORCE);

  // Set up dynamic metadata with JWT claims structure.
  setNestedDynamicMetadata("envoy.filters.http.jwt_authn", {"payload", "sub"}, "user@example.com");
  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);
  McpRouterFilter filter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  // Subject extraction should succeed - verify no 403 is returned.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(testing::_, testing::_))
      .Times(AnyNumber())
      .WillRepeatedly(testing::Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_NE("403", headers.getStatusValue());
      }));

  filter.decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter.decodeData(buffer, true);
}

// Verifies subject extraction fails when metadata path not found.
TEST_F(McpRouterFilterTest, MetadataSubjectExtractionNotFound) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  auto* metadata_key = identity->mutable_identity()->mutable_dynamic_metadata()->mutable_key();
  metadata_key->set_key("envoy.filters.http.jwt_authn");
  metadata_key->add_path()->set_key("payload");
  metadata_key->add_path()->set_key("sub");
  identity->mutable_validation()->set_mode(
      envoy::extensions::filters::http::mcp_router::v3::ValidationPolicy::ENFORCE);

  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);
  McpRouterFilter filter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  // Expect 403 due to missing subject in ENFORCE mode.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(testing::_, testing::_))
      .WillOnce(testing::Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("403", headers.getStatusValue());
      }));

  filter.decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter.decodeData(buffer, true);
}

// Verifies subject extraction fails when metadata value is not a string.
TEST_F(McpRouterFilterTest, MetadataSubjectExtractionNotString) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  auto* metadata_key = identity->mutable_identity()->mutable_dynamic_metadata()->mutable_key();
  metadata_key->set_key("envoy.filters.http.jwt_authn");
  metadata_key->add_path()->set_key("payload");
  metadata_key->add_path()->set_key("sub");
  identity->mutable_validation()->set_mode(
      envoy::extensions::filters::http::mcp_router::v3::ValidationPolicy::ENFORCE);

  // Set metadata with a number value instead of string.
  setNestedDynamicMetadataNumber("envoy.filters.http.jwt_authn", {"payload", "sub"}, 12345);
  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);
  McpRouterFilter filter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  // Expect 403 due to non-string subject value in ENFORCE mode.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(testing::_, testing::_))
      .WillOnce(testing::Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("403", headers.getStatusValue());
      }));

  filter.decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter.decodeData(buffer, true);
}

// Verifies DISABLED mode proceeds even when metadata subject not found.
TEST_F(McpRouterFilterTest, MetadataSubjectExtractionDisabledModeProceeds) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto* identity = proto_config.mutable_session_identity();
  auto* metadata_key = identity->mutable_identity()->mutable_dynamic_metadata()->mutable_key();
  metadata_key->set_key("envoy.filters.http.jwt_authn");
  metadata_key->add_path()->set_key("payload");
  metadata_key->add_path()->set_key("sub");

  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);
  McpRouterFilter filter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  // DISABLED mode should proceed with anonymous session - verify no 403 is returned.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(testing::_, testing::_))
      .Times(AnyNumber())
      .WillRepeatedly(testing::Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_NE("403", headers.getStatusValue());
      }));

  filter.decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter.decodeData(buffer, true);
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
