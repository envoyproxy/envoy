#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_socket_interface.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using testing::HasSubstr;

using DynamicModuleSocketInterfaceProto =
    envoy::extensions::network::socket_interface::dynamic_modules::v3::DynamicModuleSocketInterface;

class DynamicModuleSocketInterfaceConfigTest : public testing::Test {
public:
  static DynamicModuleSocketInterfaceProto makeProto(const std::string& module_name,
                                                     const std::string& interface_name) {
    DynamicModuleSocketInterfaceProto proto_config;
    proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
        Extensions::DynamicModules::testSharedObjectPath(module_name, "c"));
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_socket_interface_name(interface_name);
    return proto_config;
  }

  absl::Status createExtension(const DynamicModuleSocketInterfaceProto& proto_config) {
    auto interface = std::make_unique<DynamicModuleSocketInterface>();
    auto result = interface->createBootstrapExtension(proto_config, context_);
    if (!result.ok()) {
      return result.status();
    }
    // The extension clears its back pointer in its destructor, so the interface it points at must
    // outlive it. interfaces_ is declared before extensions_, so it is destroyed after them.
    interfaces_.push_back(std::move(interface));
    extensions_.push_back(std::move(result.value()));
    return absl::OkStatus();
  }

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::vector<std::unique_ptr<DynamicModuleSocketInterface>> interfaces_;
  std::vector<Server::BootstrapExtensionPtr> extensions_;
};

TEST_F(DynamicModuleSocketInterfaceConfigTest, FactoryIsRegistered) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(
          "envoy.extensions.network.socket_interface.dynamic_modules");
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("envoy.extensions.network.socket_interface.dynamic_modules", factory->name());
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleSocketInterfaceProto*>(
                         factory->createEmptyConfigProto().get()));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, ModuleConfigNewReturnsNull) {
  EXPECT_THAT(createExtension(makeProto("socket_interface_config_new_fail", "interface")),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Failed to initialize dynamic module socket interface config"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, ModuleNotFound) {
  DynamicModuleSocketInterfaceProto proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("does_not_exist");
  proto_config.set_socket_interface_name("interface");
  EXPECT_THAT(createExtension(proto_config), HasStatus(absl::StatusCode::kInvalidArgument,
                                                       HasSubstr("Failed to load dynamic module")));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, RemoteModuleSourceRejected) {
  DynamicModuleSocketInterfaceProto proto_config;
  auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
  remote->mutable_http_uri()->set_uri("http://example.com/libsocket_interface_test.so");
  remote->mutable_http_uri()->set_cluster("cluster");
  remote->mutable_http_uri()->mutable_timeout()->set_seconds(1);
  remote->set_sha256("0123456789abcdef");
  proto_config.set_socket_interface_name("interface");
  // Rejected before the loader runs, so a cached module or a cache miss that would fetch through
  // the cluster manager is never reached.
  EXPECT_THAT(createExtension(proto_config),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Dynamic module socket interface does not support remote module sources"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, SecondExtensionRejected) {
  DynamicModuleSocketInterface interface;
  auto first = interface.createBootstrapExtension(
      makeProto("socket_interface_test", "socket_interface_test"), context_);
  ASSERT_TRUE(first.ok()) << first.status().message();
  EXPECT_THAT(interface
                  .createBootstrapExtension(
                      makeProto("socket_interface_test", "socket_interface_test"), context_)
                  .status(),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Only one envoy.extensions.network.socket_interface."
                                  "dynamic_modules bootstrap extension can be configured")));
  // Once the first is gone, a new one can take its place.
  first.value().reset();
  EXPECT_TRUE(interface
                  .createBootstrapExtension(
                      makeProto("socket_interface_test", "socket_interface_test"), context_)
                  .ok());
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingRequiredHook) {
  for (const auto& [module, hook] : std::vector<std::pair<std::string, std::string>>{
           {"socket_interface_missing_config_new", "config_new"},
           {"socket_interface_missing_config_destroy", "config_destroy"},
           {"socket_interface_missing_ip_family_supported", "ip_family_supported"},
           {"socket_interface_missing_socket", "socket"},
           {"socket_interface_missing_socket_close", "socket_close"},
       }) {
    EXPECT_THAT(createExtension(makeProto(module, "interface")).message(),
                HasSubstr(absl::StrCat("Failed to resolve symbol ",
                                       "envoy_dynamic_module_on_socket_interface_", hook)))
        << module;
  }
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, IoHooksMustBeComplete) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_io_read", "interface")).message(),
      HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_socket_interface_io_read in "
                "dynamic module. The I/O hooks must be implemented together."));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingSetSocketHandle) {
  // The module implements the read and write hooks, so only the readiness handle hook is missing.
  EXPECT_THAT(createExtension(makeProto("socket_interface_missing_set_socket_handle", "interface"))
                  .message(),
              HasSubstr("Failed to resolve symbol "
                        "envoy_dynamic_module_on_socket_interface_set_socket_handle"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingSocketDuplicate) {
  EXPECT_THAT(createExtension(makeProto("socket_interface_missing_socket_duplicate", "interface"))
                  .message(),
              HasSubstr("Failed to resolve symbol "
                        "envoy_dynamic_module_on_socket_interface_socket_duplicate"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, CreationOnlyModuleNeedsNoIoHooks) {
  EXPECT_OK(createExtension(makeProto("socket_interface_creation_only", "interface")));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, InvalidSocketInterfaceConfig) {
  auto proto_config = makeProto("socket_interface_test", "interface");
  proto_config.mutable_socket_interface_config()->set_type_url(
      "type.googleapis.com/google.protobuf.Struct");
  proto_config.mutable_socket_interface_config()->set_value("\x0f");
  EXPECT_THAT(createExtension(proto_config),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Failed to parse socket interface config: ")));
}

} // namespace
} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
