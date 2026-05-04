#include <cstring>

#include "envoy/extensions/bootstrap/reverse_tunnel/reporter/dynamic_modules/v3/dynamic_module_reporter.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/dynamic_module_reporter.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// In-process records populated by the stub function pointers below. Allows
// dispatch-method tests to assert each ABI hook fires with the right buffers
// without involving an actual dlopen'd module.
struct StubRecord {
  bool reporter_destroyed = false;
  int server_initialized_calls = 0;
  std::vector<std::tuple<std::string, std::string, std::string>> connected_calls;
  std::vector<std::pair<std::string, std::string>> disconnected_calls;
};

namespace {
StubRecord* g_record = nullptr;
int g_module_reporter_handle = 0;

envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr
stub_new(envoy_dynamic_module_type_envoy_buffer /*reporter_config*/) {
  return &g_module_reporter_handle;
}
void stub_destroy(envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr ptr) {
  ASSERT(ptr == &g_module_reporter_handle);
  g_record->reporter_destroyed = true;
}
void stub_server_initialized(envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr ptr) {
  ASSERT(ptr == &g_module_reporter_handle);
  g_record->server_initialized_calls++;
}
std::string buf_to_string(envoy_dynamic_module_type_envoy_buffer buf) {
  return std::string(buf.ptr, buf.length);
}
void stub_connected(envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr ptr,
                    envoy_dynamic_module_type_envoy_buffer node_id,
                    envoy_dynamic_module_type_envoy_buffer cluster_id,
                    envoy_dynamic_module_type_envoy_buffer tenant_id) {
  ASSERT(ptr == &g_module_reporter_handle);
  g_record->connected_calls.emplace_back(buf_to_string(node_id), buf_to_string(cluster_id),
                                         buf_to_string(tenant_id));
}
void stub_disconnected(envoy_dynamic_module_type_reverse_tunnel_reporter_module_ptr ptr,
                       envoy_dynamic_module_type_envoy_buffer node_id,
                       envoy_dynamic_module_type_envoy_buffer cluster_id) {
  ASSERT(ptr == &g_module_reporter_handle);
  g_record->disconnected_calls.emplace_back(buf_to_string(node_id), buf_to_string(cluster_id));
}
} // namespace

class DynamicModuleReverseTunnelReporterDispatchTest : public testing::Test {
protected:
  void SetUp() override {
    record_ = std::make_unique<StubRecord>();
    g_record = record_.get();
  }
  void TearDown() override { g_record = nullptr; }

  std::unique_ptr<StubRecord> record_;
};

TEST_F(DynamicModuleReverseTunnelReporterDispatchTest,
       LifecycleDispatchesEveryHookToInModuleReporter) {
  {
    DynamicModuleReverseTunnelReporter reporter(/*dynamic_module=*/nullptr, &stub_new,
                                                &stub_destroy, &stub_server_initialized,
                                                &stub_connected, &stub_disconnected,
                                                /*reporter_config=*/"cfg");

    reporter.onServerInitialized();
    reporter.reportConnectionEvent("node-a", "cluster-a", "tenant-1");
    reporter.reportConnectionEvent("node-b", "cluster-b", "");
    reporter.reportDisconnectionEvent("node-a", "cluster-a");
  }

  using ConnectedTuple = std::tuple<std::string, std::string, std::string>;
  using DisconnectedPair = std::pair<std::string, std::string>;
  EXPECT_EQ(record_->server_initialized_calls, 1);
  ASSERT_EQ(record_->connected_calls.size(), 2u);
  EXPECT_EQ(record_->connected_calls[0], ConnectedTuple("node-a", "cluster-a", "tenant-1"));
  EXPECT_EQ(record_->connected_calls[1], ConnectedTuple("node-b", "cluster-b", ""));
  ASSERT_EQ(record_->disconnected_calls.size(), 1u);
  EXPECT_EQ(record_->disconnected_calls[0], DisconnectedPair("node-a", "cluster-a"));
  EXPECT_TRUE(record_->reporter_destroyed);
}

TEST_F(DynamicModuleReverseTunnelReporterDispatchTest, OptionalHooksAreNoopWhenNullptr) {
  // server_initialized / connected / disconnected are optional; if the module
  // does not export them, dispatch is a no-op.
  {
    DynamicModuleReverseTunnelReporter reporter(/*dynamic_module=*/nullptr, &stub_new,
                                                &stub_destroy, /*on_server_initialized=*/nullptr,
                                                /*on_connected=*/nullptr,
                                                /*on_disconnected=*/nullptr,
                                                /*reporter_config=*/"");
    reporter.onServerInitialized();
    reporter.reportConnectionEvent("n", "c", "t");
    reporter.reportDisconnectionEvent("n", "c");
  }

  EXPECT_EQ(record_->server_initialized_calls, 0);
  EXPECT_TRUE(record_->connected_calls.empty());
  EXPECT_TRUE(record_->disconnected_calls.empty());
  EXPECT_TRUE(record_->reporter_destroyed);
}

class DynamicModuleReverseTunnelReporterFactoryTest : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  void setSearchPath() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", testDataDir(), 1);
  }
  void unsetSearchPath() { TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH"); }

  envoy::extensions::bootstrap::reverse_tunnel::reporter::dynamic_modules::v3::
      DynamicModuleReverseTunnelReporter
      makeProto(absl::string_view module_name) {
    envoy::extensions::bootstrap::reverse_tunnel::reporter::dynamic_modules::v3::
        DynamicModuleReverseTunnelReporter proto;
    proto.mutable_dynamic_module_config()->set_name(std::string(module_name));
    return proto;
  }

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(DynamicModuleReverseTunnelReporterFactoryTest, NameAndEmptyConfigProto) {
  DynamicModuleReverseTunnelReporterFactory factory;
  EXPECT_EQ(factory.name(), "envoy.extensions.reverse_tunnel.reporting_service.dynamic_modules");
  EXPECT_NE(factory.createEmptyConfigProto(), nullptr);
}

TEST_F(DynamicModuleReverseTunnelReporterFactoryTest, DynamicModuleLoadFails) {
  DynamicModuleReverseTunnelReporterFactory factory;
  setSearchPath();
  auto proto = makeProto("nonexistent_module");
  ProtobufTypes::MessagePtr msg(proto.New());
  msg->CopyFrom(proto);
  EXPECT_THROW_WITH_REGEX(factory.createReporter(context_, std::move(msg)), EnvoyException,
                          "Failed to load dynamic module:.*");
  unsetSearchPath();
}

TEST_F(DynamicModuleReverseTunnelReporterFactoryTest, MissingRequiredAbiHooks) {
  DynamicModuleReverseTunnelReporterFactory factory;
  setSearchPath();
  auto proto = makeProto("reverse_tunnel_reporter_missing_required");
  ProtobufTypes::MessagePtr msg(proto.New());
  msg->CopyFrom(proto);
  EXPECT_THROW_WITH_REGEX(factory.createReporter(context_, std::move(msg)), EnvoyException,
                          "missing required reverse tunnel reporter ABI hooks.*");
  unsetSearchPath();
}

TEST_F(DynamicModuleReverseTunnelReporterFactoryTest, BadReporterConfigAny) {
  DynamicModuleReverseTunnelReporterFactory factory;
  setSearchPath();
  auto proto = makeProto("reverse_tunnel_reporter_no_op");
  // Set a StringValue type_url with bytes that don't decode as a StringValue
  // protobuf. MessageUtil::anyToBytes calls unpackTo<StringValue> for this
  // type_url, which returns an InvalidArgument status for malformed payloads.
  proto.mutable_reporter_config()->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  proto.mutable_reporter_config()->set_value(std::string("\xff\xff\xff\xff\xff", 5));
  ProtobufTypes::MessagePtr msg(proto.New());
  msg->CopyFrom(proto);
  EXPECT_THROW_WITH_REGEX(factory.createReporter(context_, std::move(msg)), EnvoyException,
                          "Failed to parse reporter config:.*");
  unsetSearchPath();
}

TEST_F(DynamicModuleReverseTunnelReporterFactoryTest, CreateReporterSucceeds) {
  DynamicModuleReverseTunnelReporterFactory factory;
  setSearchPath();
  auto proto = makeProto("reverse_tunnel_reporter_no_op");
  ProtobufTypes::MessagePtr msg(proto.New());
  msg->CopyFrom(proto);
  auto reporter = factory.createReporter(context_, std::move(msg));
  ASSERT_NE(reporter, nullptr);

  // Drive the public surface; the no_op .so accepts each call without effect.
  reporter->onServerInitialized();
  reporter->reportConnectionEvent("n", "c", "t");
  reporter->reportDisconnectionEvent("n", "c");
  unsetSearchPath();
}

TEST_F(DynamicModuleReverseTunnelReporterFactoryTest, CreateReporterSucceedsWithStringValueConfig) {
  DynamicModuleReverseTunnelReporterFactory factory;
  setSearchPath();
  auto proto = makeProto("reverse_tunnel_reporter_no_op");
  Protobuf::StringValue raw_config;
  raw_config.set_value("hello");
  proto.mutable_reporter_config()->PackFrom(raw_config);
  ProtobufTypes::MessagePtr msg(proto.New());
  msg->CopyFrom(proto);
  auto reporter = factory.createReporter(context_, std::move(msg));
  EXPECT_NE(reporter, nullptr);
  unsetSearchPath();
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
