#include <string>

#include "envoy/extensions/resource_monitors/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/dynamic_modules/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/options.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using testing::HasSubstr;

using DynamicModuleResourceMonitorProto =
    envoy::extensions::resource_monitors::dynamic_modules::v3::DynamicModuleResourceMonitor;

class MockResourceUpdateCallbacks : public Server::ResourceUpdateCallbacks {
public:
  MOCK_METHOD(void, onSuccess, (const Server::ResourceUsage& usage));
  MOCK_METHOD(void, onFailure, (const absl::Status& error));
};

class DynamicModuleResourceMonitorFactoryTest : public testing::Test {
public:
  DynamicModuleResourceMonitorFactoryTest()
      : api_(Api::createApiForTest()),
        context_(dispatcher_, options_, *api_, ProtobufMessage::getStrictValidationVisitor(),
                 runtime_) {}

  static DynamicModuleResourceMonitorProto makeProto(const std::string& module_name,
                                                     const std::string& language,
                                                     const std::string& resource_monitor_name) {
    DynamicModuleResourceMonitorProto proto_config;
    proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
        Extensions::DynamicModules::testSharedObjectPath(module_name, language));
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_resource_monitor_name(resource_monitor_name);
    return proto_config;
  }

  absl::StatusOr<Server::ResourceMonitorPtr>
  createMonitor(const DynamicModuleResourceMonitorProto& proto_config) {
    return factory_.createResourceMonitor(proto_config, context_);
  }

  DynamicModuleResourceMonitorFactory factory_;
  Api::ApiPtr api_;
  testing::StrictMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<Server::MockOptions> options_;
  testing::StrictMock<Runtime::MockLoader> runtime_;
  Server::Configuration::ResourceMonitorFactoryContextImpl context_;
};

TEST_F(DynamicModuleResourceMonitorFactoryTest, FactoryIsRegistered) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.dynamic_modules");
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("envoy.resource_monitors.dynamic_modules", factory->name());
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleResourceMonitorProto*>(
                         factory->createEmptyConfigProto().get()));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, CreatesMonitor) {
  auto proto_config = makeProto("resource_monitor_test", "rust", "runtime_pressure");
  Protobuf::StringValue runtime_key;
  runtime_key.set_value("test.pressure");
  ASSERT_TRUE(proto_config.mutable_resource_monitor_config()->PackFrom(runtime_key));

  auto monitor = createMonitor(proto_config);
  ASSERT_OK(monitor.status());
  ASSERT_NE(nullptr, monitor.value());

  // The server runtime is not reachable here, so the module reports the default pressure.
  testing::StrictMock<MockResourceUpdateCallbacks> callbacks;
  EXPECT_CALL(callbacks, onSuccess(Server::ResourceUsage{0}));
  monitor.value()->updateResourceUsage(callbacks);
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, PassesEmptyConfigWhenUnset) {
  // The module accepts the empty_config name only with an empty configuration.
  auto proto_config = makeProto("resource_monitor_test", "rust", "empty_config");
  EXPECT_OK(createMonitor(proto_config).status());

  Protobuf::StringValue config;
  config.set_value("x");
  ASSERT_TRUE(proto_config.mutable_resource_monitor_config()->PackFrom(config));
  EXPECT_THAT(createMonitor(proto_config).status(),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Failed to initialize dynamic module resource monitor config"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, ModuleRejectsName) {
  EXPECT_THAT(createMonitor(makeProto("resource_monitor_test", "rust", "unknown")).status(),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Failed to initialize dynamic module resource monitor config"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, ModulePanicsWhileCreatingConfig) {
  EXPECT_THAT(createMonitor(makeProto("resource_monitor_test", "rust", "panic")).status(),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Failed to initialize dynamic module resource monitor config"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, ModuleNotFound) {
  DynamicModuleResourceMonitorProto proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("does_not_exist");
  proto_config.set_resource_monitor_name("monitor");
  EXPECT_THAT(createMonitor(proto_config).status().message(),
              HasSubstr("Failed to load dynamic module"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, RemoteModuleSourceRejected) {
  DynamicModuleResourceMonitorProto proto_config;
  auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("http://example.com/libresource_monitor_test.so");
  remote->mutable_http_uri()->set_cluster("cluster");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(1);
  remote->set_sha256("0123456789abcdef");
  proto_config.set_resource_monitor_name("monitor");
  EXPECT_THAT(createMonitor(proto_config).status(),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Remote module sources require a factory context"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, MissingConfigNew) {
  EXPECT_THAT(
      createMonitor(makeProto("resource_monitor_missing_config_new", "c", "monitor"))
          .status()
          .message(),
      HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_resource_monitor_config_new"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, MissingConfigDestroy) {
  EXPECT_THAT(
      createMonitor(makeProto("resource_monitor_missing_config_destroy", "c", "monitor"))
          .status()
          .message(),
      HasSubstr(
          "Failed to resolve symbol envoy_dynamic_module_on_resource_monitor_config_destroy"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, MissingUpdateResourceUsage) {
  EXPECT_THAT(
      createMonitor(makeProto("resource_monitor_missing_update_resource_usage", "c", "monitor"))
          .status()
          .message(),
      HasSubstr("Failed to resolve symbol "
                "envoy_dynamic_module_on_resource_monitor_update_resource_usage"));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, InvalidResourceMonitorConfig) {
  auto proto_config = makeProto("resource_monitor_no_op", "c", "monitor");
  proto_config.mutable_resource_monitor_config()->set_type_url(
      "type.googleapis.com/google.protobuf.Struct");
  proto_config.mutable_resource_monitor_config()->set_value("\x0f");
  EXPECT_THAT(createMonitor(proto_config).status(),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Failed to parse resource monitor config: ")));
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, MissingResourceMonitorName) {
  auto proto_config = makeProto("resource_monitor_no_op", "c", "monitor");
  proto_config.clear_resource_monitor_name();
  EXPECT_THROW_WITH_REGEX(std::ignore = createMonitor(proto_config), EnvoyException,
                          "ResourceMonitorName");
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, MissingDynamicModuleConfig) {
  auto proto_config = makeProto("resource_monitor_no_op", "c", "monitor");
  proto_config.clear_dynamic_module_config();
  EXPECT_THROW_WITH_REGEX(std::ignore = createMonitor(proto_config), EnvoyException,
                          "DynamicModuleConfig");
}

} // namespace
} // namespace DynamicModules
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
