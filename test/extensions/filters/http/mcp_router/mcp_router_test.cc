#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.h"
#include "envoy/http/async_client.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::ReturnRef;

// Verifies parseMethodString correctly maps MCP method strings to enum values.
TEST(ParseMethodStringTest, AllMethods) {
  EXPECT_EQ(parseMethodString("initialize"), McpMethod::Initialize);
  EXPECT_EQ(parseMethodString("tools/list"), McpMethod::ToolsList);
  EXPECT_EQ(parseMethodString("tools/call"), McpMethod::ToolsCall);
  EXPECT_EQ(parseMethodString("resources/list"), McpMethod::ResourcesList);
  EXPECT_EQ(parseMethodString("resources/read"), McpMethod::ResourcesRead);
  EXPECT_EQ(parseMethodString("resources/subscribe"), McpMethod::ResourcesSubscribe);
  EXPECT_EQ(parseMethodString("resources/unsubscribe"), McpMethod::ResourcesUnsubscribe);
  EXPECT_EQ(parseMethodString("prompts/list"), McpMethod::PromptsList);
  EXPECT_EQ(parseMethodString("prompts/get"), McpMethod::PromptsGet);
  EXPECT_EQ(parseMethodString("completion/complete"), McpMethod::CompletionComplete);
  EXPECT_EQ(parseMethodString("logging/setLevel"), McpMethod::LoggingSetLevel);
  EXPECT_EQ(parseMethodString("ping"), McpMethod::Ping);
  // Notifications (client -> server only).
  EXPECT_EQ(parseMethodString("notifications/initialized"), McpMethod::NotificationInitialized);
  EXPECT_EQ(parseMethodString("notifications/cancelled"), McpMethod::NotificationCancelled);
  EXPECT_EQ(parseMethodString("notifications/roots/list_changed"),
            McpMethod::NotificationRootsListChanged);
  EXPECT_EQ(parseMethodString("unknown_method"), McpMethod::Unknown);
  EXPECT_EQ(parseMethodString(""), McpMethod::Unknown);
}

class McpRouterConfigTest : public testing::Test {
protected:
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Stats::TestUtil::TestStore store_;
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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);

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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);

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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);

  const McpBackendConfig* backend = config.findBackend("test");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->path, "/mcp");
}

// Verifies metadata namespace defaults to "envoy.filters.http.mcp" when not specified.
TEST_F(McpRouterConfigTest, DefaultMetadataNamespace) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;

  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
  EXPECT_EQ(config.metadataNamespace(), "envoy.filters.http.mcp");
}

// Verifies McpRouterClusterConfigImpl parses ClusterConfig proto correctly.
TEST_F(McpRouterConfigTest, McpRouterClusterConfigImplParsesProto) {
  envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("cluster_backend");
  server->mutable_mcp_cluster()->set_cluster("target_cluster");
  server->mutable_mcp_cluster()->set_path("/custom_mcp");
  server->mutable_mcp_cluster()->mutable_timeout()->set_seconds(10);

  envoy::extensions::filters::http::mcp_router::v3::McpRouter base_proto;
  auto base_config = std::make_shared<McpRouterConfigImpl>(base_proto, "test.", *store_.rootScope(),
                                                           factory_context_);

  McpRouterClusterConfigImpl cluster_config(proto_config, base_config);

  EXPECT_EQ(cluster_config.backends().size(), 1);
  EXPECT_FALSE(cluster_config.isMultiplexing());

  const McpBackendConfig* backend = cluster_config.findBackend("cluster_backend");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->name, "cluster_backend");
  EXPECT_EQ(backend->cluster_name, "target_cluster");
  EXPECT_EQ(backend->path, "/custom_mcp");
  EXPECT_EQ(backend->timeout, std::chrono::milliseconds(10000));
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
  auto parsed = SessionCodec::parseCompositeSessionId(composite);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->subject, "user1");
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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
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

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
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
    return std::make_shared<McpRouterConfigImpl>(proto_config, std::string("test."),
                                                 *store_.rootScope(), factory_context_);
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

  void setMcpMethodMetadata(const std::string& method, int64_t id = 1,
                            const std::string& metadata_namespace = "envoy.filters.http.mcp") {
    auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())[metadata_namespace];
    (*mcp_metadata.mutable_fields())["method"].set_string_value(method);
    (*mcp_metadata.mutable_fields())["id"].set_number_value(static_cast<double>(id));
  }

  void setMcpToolCallMetadata(const std::string& tool_name, int64_t id = 1,
                              const std::string& metadata_namespace = "envoy.filters.http.mcp") {
    auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())[metadata_namespace];
    (*mcp_metadata.mutable_fields())["method"].set_string_value("tools/call");
    (*mcp_metadata.mutable_fields())["id"].set_number_value(static_cast<double>(id));
    auto& params = (*mcp_metadata.mutable_fields())["params"];
    (*params.mutable_struct_value()->mutable_fields())["name"].set_string_value(tool_name);
  }

  void setupMockAsyncClient(
      std::vector<Http::AsyncClient::StreamCallbacks*>& http_callbacks,
      std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>>& http_streams) {
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"test_cluster", "time_cluster", "calc_cluster"});
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .async_client_,
                start(_, _))
        .WillRepeatedly(
            [&http_callbacks, &http_streams](Http::AsyncClient::StreamCallbacks& callbacks,
                                             const Http::AsyncClient::StreamOptions&) {
              http_callbacks.push_back(&callbacks);
              http_streams.emplace_back(std::make_unique<NiceMock<Http::MockAsyncClientStream>>());
              return http_streams.back().get();
            });
  }

  void simulateBackendResponse(Http::AsyncClient::StreamCallbacks* cb, const std::string& body,
                               const std::string& session_id = "",
                               const std::string& content_type = "application/json") {
    auto response_headers = Http::ResponseHeaderMapImpl::create();
    response_headers->setStatus(200);
    response_headers->setContentType(content_type);
    if (!session_id.empty()) {
      response_headers->addCopy(Http::LowerCaseString("mcp-session-id"), session_id);
    }
    cb->onHeaders(std::move(response_headers), false);
    Buffer::OwnedImpl response_data(body);
    cb->onData(response_data, true);
  }

  void simulateBackendError(Http::AsyncClient::StreamCallbacks* cb, uint64_t status_code = 500) {
    auto response_headers = Http::ResponseHeaderMapImpl::create();
    response_headers->setStatus(status_code);
    response_headers->setContentType("text/plain");
    cb->onHeaders(std::move(response_headers), false);
    Buffer::OwnedImpl response_data("error");
    cb->onData(response_data, true);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
  Stats::TestUtil::TestStore store_;
};

TEST_F(McpRouterFilterTest, UsingClusterConfigForListOfServers) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);

  // These vectors have to outlive the filter object
  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  McpRouterFilter filter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);

  envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig cluster_config;
  auto* backend = cluster_config.add_servers();
  backend->set_name("backend");
  backend->mutable_mcp_cluster()->set_cluster("backend_cluster");

  (*decoder_callbacks_.cluster_info_->metadata_
        .mutable_typed_filter_metadata())["envoy.clusters.mcp_multicluster"]
      .PackFrom(cluster_config);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(testing::Invoke([](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_NE("403", headers.getStatusValue());
      }));

  filter.decodeHeaders(headers, false);

  // Add "backend_cluster" to the list of cluster manager.
  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"backend_cluster"});
  // MCP router is expected to use list of servers from the cluster metadata, which contains
  // "backend_cluster". If it does use cluster metadata, the cluster lookup will succeed and
  // a new HTTP stream will be opened. Otherwise this expectation will fail.
  EXPECT_CALL(
      factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.async_client_,
      start(_, _))
      .WillRepeatedly(
          [&http_callbacks, &http_streams](Http::AsyncClient::StreamCallbacks& callbacks,
                                           const Http::AsyncClient::StreamOptions&) {
            http_callbacks.push_back(&callbacks);
            http_streams.emplace_back(std::make_unique<NiceMock<Http::MockAsyncClientStream>>());
            return http_streams.back().get();
          });

  // This test only validates that correct cluster is selected.
  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter.decodeData(buffer, true);
}

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

// Verifies tools/list aggregation preserves all MCP tool attributes.
TEST(AggregateToolsListTest, PreservesAllToolAttributes) {
  // Backend response with all MCP tool attributes.
  const std::string backend_response = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "tools": [{
        "name": "get_weather",
        "title": "Weather Tool",
        "description": "Get weather information",
        "inputSchema": {
          "type": "object",
          "properties": {
            "location": {"type": "string", "description": "City name"},
            "unit": {"type": "string", "enum": ["celsius", "fahrenheit"]}
          },
          "required": ["location"]
        },
        "outputSchema": {
          "type": "object",
          "properties": {
            "temperature": {"type": "number"},
            "condition": {"type": "string"}
          }
        },
        "annotations": {
          "audience": ["user"],
          "readOnly": true
        }
      }]
    }
  })";

  auto parsed = Json::Factory::loadFromString(backend_response);
  ASSERT_TRUE(parsed.ok());

  auto result = (*parsed)->getObject("result");
  ASSERT_TRUE(result.ok());

  auto tools = (*result)->getObjectArray("tools");
  ASSERT_TRUE(tools.ok());
  ASSERT_EQ(tools->size(), 1);

  const auto& tool = (*tools)[0];
  ASSERT_TRUE(tool != nullptr);

  // Verify all attributes are present.
  auto name = tool->getString("name");
  EXPECT_TRUE(name.ok());
  EXPECT_EQ(*name, "get_weather");

  auto title = tool->getString("title");
  EXPECT_TRUE(title.ok());
  EXPECT_EQ(*title, "Weather Tool");

  auto desc = tool->getString("description");
  EXPECT_TRUE(desc.ok());
  EXPECT_EQ(*desc, "Get weather information");

  auto input_schema = tool->getObject("inputSchema");
  EXPECT_TRUE(input_schema.ok());
  EXPECT_TRUE(*input_schema != nullptr);

  // Verify nested inputSchema properties are present.
  auto props = (*input_schema)->getObject("properties");
  EXPECT_TRUE(props.ok());

  auto output_schema = tool->getObject("outputSchema");
  EXPECT_TRUE(output_schema.ok());

  auto annotations = tool->getObject("annotations");
  EXPECT_TRUE(annotations.ok());
}

// Verifies tool JSON serialization preserves nested inputSchema.
TEST(AggregateToolsListTest, SerializationPreservesNestedInputSchema) {
  const std::string tool_json = R"({
    "name": "test_tool",
    "inputSchema": {
      "type": "object",
      "properties": {
        "query": {"type": "string"},
        "count": {"type": "integer", "minimum": 1, "maximum": 100}
      },
      "required": ["query"]
    }
  })";

  auto parsed = Json::Factory::loadFromString(tool_json);
  ASSERT_TRUE(parsed.ok());

  // Serialize and re-parse to verify round-trip.
  std::string serialized = (*parsed)->asJsonString();

  auto reparsed = Json::Factory::loadFromString(serialized);
  ASSERT_TRUE(reparsed.ok());

  auto input_schema = (*reparsed)->getObject("inputSchema");
  ASSERT_TRUE(input_schema.ok());

  auto props = (*input_schema)->getObject("properties");
  ASSERT_TRUE(props.ok());

  auto query_prop = (*props)->getObject("query");
  EXPECT_TRUE(query_prop.ok());

  auto count_prop = (*props)->getObject("count");
  EXPECT_TRUE(count_prop.ok());

  // Verify the nested properties are preserved.
  auto count_type = (*count_prop)->getString("type");
  EXPECT_TRUE(count_type.ok());
  EXPECT_EQ(*count_type, "integer");
}

// Verifies lazy_initialization config field defaults to false and can be enabled.
TEST_F(McpRouterConfigTest, LazyInitializationDefault) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
  EXPECT_FALSE(config.lazyInitialization());
}

TEST_F(McpRouterConfigTest, LazyInitializationEnabled) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");
  proto_config.set_lazy_initialization(true);

  McpRouterConfigImpl config(proto_config, "test.", *store_.rootScope(), factory_context_);
  EXPECT_TRUE(config.lazyInitialization());
}

// Verifies McpRouterClusterConfigImpl delegates lazyInitialization to base.
TEST_F(McpRouterConfigTest, ClusterConfigDelegatesLazyInit) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter base_proto;
  base_proto.set_lazy_initialization(true);
  auto base_config = std::make_shared<McpRouterConfigImpl>(base_proto, "test.", *store_.rootScope(),
                                                           factory_context_);

  envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig cluster_proto;
  auto* server = cluster_proto.add_servers();
  server->set_name("cluster_backend");
  server->mutable_mcp_cluster()->set_cluster("target_cluster");

  McpRouterClusterConfigImpl cluster_config(cluster_proto, base_config);
  EXPECT_TRUE(cluster_config.lazyInitialization());
}

// Verifies lazy initialization responds immediately to initialize without backend fanout.
TEST_F(McpRouterFilterTest, LazyInitializeRespondsImmediately) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);
  McpRouterFilter filter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        auto session_header = headers.get(Http::LowerCaseString("mcp-session-id"));
        EXPECT_FALSE(session_header.empty());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  filter.decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter.decodeData(buffer, true);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init tools/call triggers backend initialization then forwards the request.
TEST_F(McpRouterFilterTest, LazyInitToolsCallInitializesBackend) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpToolCallMetadata("get_time");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  // Build a session ID with empty backend sessions (lazy init state).
  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"get_time"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  // First stream should be the lazy init (initialize) request to the backend.
  ASSERT_GE(http_callbacks.size(), 1);

  // Simulate backend init response.
  const std::string init_response =
      R"({"jsonrpc":"2.0","id":2,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  // Second stream should be the actual tools/call request.
  ASSERT_GE(http_callbacks.size(), 2);

  // Simulate tools/call response.
  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string tool_response =
      R"({"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"12:00"}]}})";
  simulateBackendResponse(http_callbacks[1], tool_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init tools/list triggers fanout initialization then forwards the list request.
TEST_F(McpRouterFilterTest, LazyInitToolsListInitializesAllBackends) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server1 = proto_config.add_servers();
  server1->set_name("time");
  server1->mutable_mcp_cluster()->set_cluster("time_cluster");
  auto* server2 = proto_config.add_servers();
  server2->set_name("calc");
  server2->mutable_mcp_cluster()->set_cluster("calc_cluster");

  setMcpMethodMetadata("tools/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  // Build a session ID with empty backend sessions.
  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"tools/list","id":3})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  // First two streams should be the lazy init fanout (initialize to both backends).
  ASSERT_GE(http_callbacks.size(), 2);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":3,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "time-session");
  simulateBackendResponse(http_callbacks[1], init_response, "calc-session");

  // Next two streams should be the actual tools/list fanout.
  ASSERT_GE(http_callbacks.size(), 4);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string time_tools =
      R"({"jsonrpc":"2.0","id":3,"result":{"tools":[{"name":"get_time"}]}})";
  const std::string calc_tools = R"({"jsonrpc":"2.0","id":3,"result":{"tools":[{"name":"add"}]}})";
  simulateBackendResponse(http_callbacks[2], time_tools);
  simulateBackendResponse(http_callbacks[3], calc_tools);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init notifications with no initialized backends respond 202 immediately.
TEST_F(McpRouterFilterTest, LazyInitNotificationNoBackends) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("notifications/initialized");

  auto config = createConfig(proto_config);
  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  // Build a session ID with empty backend sessions.
  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) {
        EXPECT_EQ("202", headers.getStatusValue());
        EXPECT_TRUE(end_stream);
        response_sent = true;
      }));

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init with eager mode disabled (default) behaves normally.
TEST_F(McpRouterFilterTest, EagerInitUnchangedWithLazyDisabled) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("initialize");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2025-06-18"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  // Eager mode should have started a backend stream (unlike lazy init which responds immediately).
  EXPECT_GE(http_callbacks.size(), 1);
}

// Verifies lazy init prompts/get triggers backend initialization.
TEST_F(McpRouterFilterTest, LazyInitPromptsGetInitializesBackend) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  // Set prompts/get metadata with prompt name.
  auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["envoy.filters.http.mcp"];
  (*mcp_metadata.mutable_fields())["method"].set_string_value("prompts/get");
  (*mcp_metadata.mutable_fields())["id"].set_number_value(4);
  auto& params = (*mcp_metadata.mutable_fields())["params"];
  (*params.mutable_struct_value()->mutable_fields())["name"].set_string_value("greeting");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"prompts/get","id":4,"params":{"name":"greeting"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  // First stream should be the lazy init request.
  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":4,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  // Second stream should be the actual prompts/get request.
  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string prompt_response =
      R"({"jsonrpc":"2.0","id":4,"result":{"messages":[{"role":"assistant","content":{"type":"text","text":"Hello!"}}]}})";
  simulateBackendResponse(http_callbacks[1], prompt_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init prompts/list triggers fanout initialization.
TEST_F(McpRouterFilterTest, LazyInitPromptsListInitializesBackends) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("prompts/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"prompts/list","id":5})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  // First stream: lazy init.
  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":5,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  // Second stream: actual prompts/list fanout.
  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string prompts_response =
      R"({"jsonrpc":"2.0","id":5,"result":{"prompts":[{"name":"greeting"}]}})";
  simulateBackendResponse(http_callbacks[1], prompts_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init resources/list triggers fanout initialization.
TEST_F(McpRouterFilterTest, LazyInitResourcesListInitializesBackends) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("resources/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"resources/list","id":6})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  // First stream: lazy init.
  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":6,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  // Second stream: actual resources/list fanout.
  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string resources_response =
      R"({"jsonrpc":"2.0","id":6,"result":{"resources":[{"uri":"file://test","name":"test"}]}})";
  simulateBackendResponse(http_callbacks[1], resources_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init resources/templates/list triggers fanout initialization.
TEST_F(McpRouterFilterTest, LazyInitResourcesTemplatesListInitializesBackends) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("resources/templates/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"resources/templates/list","id":7})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":7,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string templates_response =
      R"({"jsonrpc":"2.0","id":7,"result":{"resourceTemplates":[{"uriTemplate":"file:///{path}","name":"file"}]}})";
  simulateBackendResponse(http_callbacks[1], templates_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init logging/setLevel triggers fanout initialization.
TEST_F(McpRouterFilterTest, LazyInitLoggingSetLevelInitializesBackends) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("logging/setLevel");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"logging/setLevel","id":8,"params":{"level":"debug"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":8,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string logging_response = R"({"jsonrpc":"2.0","id":8,"result":{}})";
  simulateBackendResponse(http_callbacks[1], logging_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init resources/read triggers backend initialization.
TEST_F(McpRouterFilterTest, LazyInitResourcesReadInitializesBackend) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  // Set resources/read metadata with URI.
  auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["envoy.filters.http.mcp"];
  (*mcp_metadata.mutable_fields())["method"].set_string_value("resources/read");
  (*mcp_metadata.mutable_fields())["id"].set_number_value(9);
  auto& params = (*mcp_metadata.mutable_fields())["params"];
  (*params.mutable_struct_value()->mutable_fields())["uri"].set_string_value("file://test.txt");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"resources/read","id":9,"params":{"uri":"file://test.txt"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":9,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string resource_response =
      R"({"jsonrpc":"2.0","id":9,"result":{"contents":[{"uri":"file://test.txt","text":"hello"}]}})";
  simulateBackendResponse(http_callbacks[1], resource_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init completion/complete with ref/prompt triggers backend initialization.
TEST_F(McpRouterFilterTest, LazyInitCompletionCompleteInitializesBackend) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  // Set completion/complete metadata with ref/prompt.
  auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["envoy.filters.http.mcp"];
  (*mcp_metadata.mutable_fields())["method"].set_string_value("completion/complete");
  (*mcp_metadata.mutable_fields())["id"].set_number_value(10);
  auto& params = (*mcp_metadata.mutable_fields())["params"];
  auto& ref = (*params.mutable_struct_value()->mutable_fields())["ref"];
  (*ref.mutable_struct_value()->mutable_fields())["type"].set_string_value("ref/prompt");
  (*ref.mutable_struct_value()->mutable_fields())["name"].set_string_value("greeting");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"completion/complete","id":10,"params":{"ref":{"type":"ref/prompt","name":"greeting"},"argument":{"name":"name","value":"wo"}}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":10,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string completion_response =
      R"({"jsonrpc":"2.0","id":10,"result":{"completion":{"values":["world"]}}})";
  simulateBackendResponse(http_callbacks[1], completion_response);

  EXPECT_TRUE(response_sent);
}

// Verifies lazy init tools/call sends error when backend init fails.
TEST_F(McpRouterFilterTest, LazyInitToolsCallInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpToolCallMetadata("get_time");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"get_time"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init tools/list sends error when fanout init fails.
TEST_F(McpRouterFilterTest, LazyInitToolsListInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("tools/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"tools/list","id":3})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init prompts/get sends error when backend init fails.
TEST_F(McpRouterFilterTest, LazyInitPromptsGetInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["envoy.filters.http.mcp"];
  (*mcp_metadata.mutable_fields())["method"].set_string_value("prompts/get");
  (*mcp_metadata.mutable_fields())["id"].set_number_value(4);
  auto& params = (*mcp_metadata.mutable_fields())["params"];
  (*params.mutable_struct_value()->mutable_fields())["name"].set_string_value("greeting");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"prompts/get","id":4,"params":{"name":"greeting"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init prompts/list sends error when fanout init fails.
TEST_F(McpRouterFilterTest, LazyInitPromptsListInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("prompts/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"prompts/list","id":5})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init resources/list sends error when fanout init fails.
TEST_F(McpRouterFilterTest, LazyInitResourcesListInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("resources/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"resources/list","id":6})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init resources/read sends error when backend init fails.
TEST_F(McpRouterFilterTest, LazyInitResourcesReadInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["envoy.filters.http.mcp"];
  (*mcp_metadata.mutable_fields())["method"].set_string_value("resources/read");
  (*mcp_metadata.mutable_fields())["id"].set_number_value(9);
  auto& params = (*mcp_metadata.mutable_fields())["params"];
  (*params.mutable_struct_value()->mutable_fields())["uri"].set_string_value("file://test.txt");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"resources/read","id":9,"params":{"uri":"file://test.txt"}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init completion/complete sends error when backend init fails.
TEST_F(McpRouterFilterTest, LazyInitCompletionCompleteInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  auto& mcp_metadata = (*dynamic_metadata_.mutable_filter_metadata())["envoy.filters.http.mcp"];
  (*mcp_metadata.mutable_fields())["method"].set_string_value("completion/complete");
  (*mcp_metadata.mutable_fields())["id"].set_number_value(10);
  auto& params = (*mcp_metadata.mutable_fields())["params"];
  auto& ref = (*params.mutable_struct_value()->mutable_fields())["ref"];
  (*ref.mutable_struct_value()->mutable_fields())["type"].set_string_value("ref/prompt");
  (*ref.mutable_struct_value()->mutable_fields())["name"].set_string_value("greeting");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body =
      R"({"jsonrpc":"2.0","method":"completion/complete","id":10,"params":{"ref":{"type":"ref/prompt","name":"greeting"}}})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init resources/templates/list sends error when fanout init fails.
TEST_F(McpRouterFilterTest, LazyInitResourcesTemplatesListInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("resources/templates/list");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"resources/templates/list","id":7})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies lazy init logging/setLevel sends error when fanout init fails.
TEST_F(McpRouterFilterTest, LazyInitLoggingSetLevelInitFailure) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpMethodMetadata("logging/setLevel");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  const std::string body = R"({"jsonrpc":"2.0","method":"logging/setLevel","id":8})";
  Buffer::OwnedImpl buffer(body);
  filter->decodeData(buffer, true);

  ASSERT_GE(http_callbacks.size(), 1);

  bool error_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("500", headers.getStatusValue());
        error_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  simulateBackendError(http_callbacks[0]);

  EXPECT_TRUE(error_sent);
}

// Verifies multi-chunk data is buffered during lazy init.
TEST_F(McpRouterFilterTest, LazyInitBuffersMultiChunkData) {
  envoy::extensions::filters::http::mcp_router::v3::McpRouter proto_config;
  proto_config.set_lazy_initialization(true);
  auto* server = proto_config.add_servers();
  server->set_name("test");
  server->mutable_mcp_cluster()->set_cluster("test_cluster");

  setMcpToolCallMetadata("get_time");

  auto config = createConfig(proto_config);

  std::vector<Http::AsyncClient::StreamCallbacks*> http_callbacks;
  std::vector<std::unique_ptr<NiceMock<Http::MockAsyncClientStream>>> http_streams;

  auto filter = std::make_shared<McpRouterFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_callbacks_);

  setupMockAsyncClient(http_callbacks, http_streams);

  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite =
      SessionCodec::buildCompositeSessionId("default", "default", empty_sessions);
  std::string encoded_session = SessionCodec::encode(composite);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/mcp"},
                                         {"content-type", "application/json"},
                                         {"mcp-session-id", encoded_session}};

  filter->decodeHeaders(headers, false);

  // Send first chunk (not end_stream) — triggers lazy init.
  const std::string body_part1 = R"({"jsonrpc":"2.0","method":"tools/call",)";
  Buffer::OwnedImpl buffer1(body_part1);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter->decodeData(buffer1, false));

  // Send second chunk while lazy init is still pending — should be buffered.
  const std::string body_part2 = R"("id":2,"params":{"name":"get_time"}})";
  Buffer::OwnedImpl buffer2(body_part2);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter->decodeData(buffer2, true));

  ASSERT_GE(http_callbacks.size(), 1);

  const std::string init_response =
      R"({"jsonrpc":"2.0","id":2,"result":{"protocolVersion":"2025-06-18","capabilities":{}}})";
  simulateBackendResponse(http_callbacks[0], init_response, "backend-session-1");

  ASSERT_GE(http_callbacks.size(), 2);

  bool response_sent = false;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(testing::Invoke([&](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ("200", headers.getStatusValue());
        response_sent = true;
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));

  const std::string tool_response =
      R"({"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"12:00"}]}})";
  simulateBackendResponse(http_callbacks[1], tool_response);

  EXPECT_TRUE(response_sent);
}

// Verifies empty backend session map produces a valid session ID.
TEST_F(SessionCodecTest, EmptyBackendSessions) {
  absl::flat_hash_map<std::string, std::string> empty_sessions;
  std::string composite = SessionCodec::buildCompositeSessionId("route", "user", empty_sessions);

  auto parsed = SessionCodec::parseCompositeSessionId(composite);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->route, "route");
  EXPECT_EQ(parsed->subject, "user");
  EXPECT_TRUE(parsed->backend_sessions.empty());

  std::string encoded = SessionCodec::encode(composite);
  std::string decoded = SessionCodec::decode(encoded);
  EXPECT_EQ(decoded, composite);
}

// Verifies tools with icons array are handled correctly.
TEST(AggregateToolsListTest, IconsArrayPreserved) {
  const std::string tool_json = R"({
    "name": "tool_with_icons",
    "icons": [
      {"type": "svg", "uri": "https://example.com/icon.svg"},
      {"type": "png", "uri": "https://example.com/icon.png"}
    ]
  })";

  auto parsed = Json::Factory::loadFromString(tool_json);
  ASSERT_TRUE(parsed.ok());

  auto icons = (*parsed)->getObjectArray("icons");
  ASSERT_TRUE(icons.ok());
  EXPECT_EQ(icons->size(), 2);
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
