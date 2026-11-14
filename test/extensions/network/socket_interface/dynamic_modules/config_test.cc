#include <memory>
#include <string>

#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_socket_interface.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

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
                                                     const std::string& language,
                                                     const std::string& interface_name,
                                                     const std::string& interface_config = "") {
    DynamicModuleSocketInterfaceProto proto_config;
    proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
        Extensions::DynamicModules::testSharedObjectPath(module_name, language));
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_socket_interface_name(interface_name);
    if (!interface_config.empty()) {
      Protobuf::StringValue config;
      config.set_value(interface_config);
      EXPECT_TRUE(proto_config.mutable_socket_interface_config()->PackFrom(config));
    }
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

TEST_F(DynamicModuleSocketInterfaceConfigTest, CreatesExtensionWithCModule) {
  EXPECT_OK(createExtension(makeProto("socket_interface_test", "c", "socket_interface_test")));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, CreatesExtensionWithRustModule) {
  EXPECT_OK(createExtension(
      makeProto("socket_interface_test", "rust", "socket_interface_test", "12345")));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, ModuleRejectsConfig) {
  // The Rust module only accepts a port number as its configuration.
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_test", "rust", "socket_interface_test", "x")),
      HasStatus(absl::StatusCode::kInvalidArgument,
                "Failed to initialize dynamic module socket interface config"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, ModuleConfigNewReturnsNull) {
  EXPECT_THAT(createExtension(makeProto("socket_interface_config_new_fail", "c", "interface")),
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
  EXPECT_THAT(createExtension(proto_config),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Remote module sources require an init manager")));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingConfigNew) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_config_new", "c", "interface")).message(),
      HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_socket_interface_config_new"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingConfigDestroy) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_config_destroy", "c", "interface"))
          .message(),
      HasSubstr(
          "Failed to resolve symbol envoy_dynamic_module_on_socket_interface_config_destroy"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingIpFamilySupported) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_ip_family_supported", "c", "interface"))
          .message(),
      HasSubstr("Failed to resolve symbol "
                "envoy_dynamic_module_on_socket_interface_ip_family_supported"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingSocket) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_socket", "c", "interface")).message(),
      HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_socket_interface_socket"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingSocketClose) {
  EXPECT_THAT(createExtension(makeProto("socket_interface_missing_socket_close", "c", "interface"))
                  .message(),
              HasSubstr("Failed to resolve symbol "
                        "envoy_dynamic_module_on_socket_interface_socket_close"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, IoHooksMustBeComplete) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_io_read", "c", "interface")).message(),
      HasSubstr("The I/O hooks must be implemented together."));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, MissingSetSocketHandle) {
  EXPECT_THAT(
      createExtension(makeProto("socket_interface_missing_set_socket_handle", "c", "interface"))
          .message(),
      // The message lists every missing symbol, so match the name only.
      HasSubstr("envoy_dynamic_module_on_socket_interface_set_socket_handle"));
}

TEST_F(DynamicModuleSocketInterfaceConfigTest, InvalidSocketInterfaceConfig) {
  auto proto_config = makeProto("socket_interface_test", "c", "interface");
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
