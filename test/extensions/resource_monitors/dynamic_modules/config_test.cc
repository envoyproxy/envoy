#include <functional>
#include <memory>
#include <string>

#include "envoy/extensions/resource_monitors/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
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

  // Returns a function that reads how often the destroy hook of a C module ran. The function holds
  // the module, which keeps the counter alive because the monitor and the function share one
  // dlopen of the same path.
  static std::function<int()> configDestroyCounter(const std::string& module_name) {
    using GetConfigDestroyCountFuncType = int (*)(void);
    auto module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath(module_name, "c"),
        /*do_not_close=*/true);
    EXPECT_TRUE(module.ok());
    auto destroy_count =
        module.value()->getFunctionPointer<GetConfigDestroyCountFuncType>("getConfigDestroyCount");
    EXPECT_TRUE(destroy_count.ok());
    return [module = std::shared_ptr(std::move(module.value())), count = destroy_count.value()]() {
      return count();
    };
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
  EXPECT_EQ(nullptr, dynamic_cast<Server::SynchronousFeedbackResourceMonitor*>(monitor->get()));

  // The server runtime is not reachable here, so the module reports the default pressure.
  testing::StrictMock<MockResourceUpdateCallbacks> callbacks;
  EXPECT_CALL(callbacks, onSuccess(Server::ResourceUsage{0}));
  monitor.value()->updateResourceUsage(callbacks);
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, CreatesSynchronousFeedbackMonitor) {
  auto monitor = createMonitor(makeProto("resource_monitor_synchronous_feedback", "c", "monitor"));
  ASSERT_OK(monitor.status());
  auto* synchronous_feedback_monitor =
      dynamic_cast<Server::SynchronousFeedbackResourceMonitor*>(monitor->get());
  ASSERT_NE(nullptr, synchronous_feedback_monitor);
  EXPECT_EQ(Server::ResourceUsage{0.25}, synchronous_feedback_monitor->getResourceUsage());

  // The refresh reads the same pressure rather than calling the update hook of the module.
  testing::StrictMock<MockResourceUpdateCallbacks> callbacks;
  EXPECT_CALL(callbacks, onSuccess(Server::ResourceUsage{0.25}));
  synchronous_feedback_monitor->updateResourceUsage(callbacks);
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, SynchronousFeedbackMissingGetResourceUsage) {
  const std::string module_name =
      "resource_monitor_synchronous_feedback_missing_get_resource_usage";
  auto config_destroy_count = configDestroyCounter(module_name);
  const int destroyed_before = config_destroy_count();
  EXPECT_THAT(
      createMonitor(makeProto(module_name, "c", "monitor")).status().message(),
      HasSubstr(
          "Failed to resolve symbol envoy_dynamic_module_on_resource_monitor_get_resource_usage"));
  // Envoy rejects the configuration only after the module created its own, so it destroys it.
  EXPECT_EQ(destroyed_before + 1, config_destroy_count());
}

TEST_F(DynamicModuleResourceMonitorFactoryTest, SynchronousFeedbackWithoutLoadAccepted) {
  auto monitor = createMonitor(
      makeProto("resource_monitor_synchronous_feedback_missing_load_accepted", "c", "monitor"));
  ASSERT_OK(monitor.status());
  EXPECT_NE(nullptr, dynamic_cast<Server::SynchronousFeedbackResourceMonitor*>(monitor->get()));
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
  const std::string module_name = "resource_monitor_missing_update_resource_usage";
  auto config_destroy_count = configDestroyCounter(module_name);
  const int destroyed_before = config_destroy_count();
  EXPECT_THAT(createMonitor(makeProto(module_name, "c", "monitor")).status().message(),
              HasSubstr("Failed to resolve symbol "
                        "envoy_dynamic_module_on_resource_monitor_update_resource_usage"));
  // Envoy rejects the configuration only after the module created its own, so it destroys it.
  EXPECT_EQ(destroyed_before + 1, config_destroy_count());
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
