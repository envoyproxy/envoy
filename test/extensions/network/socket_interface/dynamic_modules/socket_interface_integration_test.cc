#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/socket_interface.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;
namespace {

using envoy::extensions::network::socket_interface::dynamic_modules::v3::
    DynamicModuleSocketInterface;

// Boots Envoy with the socket-interface dynamic module as the default interface. The module runs
// an internal echo server: when Envoy asks for a socket to the configured port, the module dials
// the echo server itself and hands Envoy the already-connected socket. The cluster endpoint is
// set to that port, so the upstream leg of a proxied request flows through the module-created
// socket and the response comes back through the module's echo server.
class DynamicModuleSocketInterfaceIntegrationTest : public BaseIntegrationTest,
                                                    public testing::Test {
public:
  DynamicModuleSocketInterfaceIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, config()) {
    use_lds_ = false;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* extension = bootstrap.mutable_bootstrap_extensions()->Add();
      extension->set_name("envoy.extensions.network.socket_interface.dynamic_modules");
      DynamicModuleSocketInterface proto_config;
      proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
          Extensions::DynamicModules::testSharedObjectPath("socket_interface_test", "rust"));
      proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
      proto_config.set_socket_interface_name("socket_interface_test");
      ASSERT_TRUE(extension->mutable_typed_config()->PackFrom(proto_config));
    });
    // The fake upstream ports are final when config modifiers run, before setPorts rewrites the
    // cluster endpoint to the same port. The module dials its echo server for sockets to that
    // port, so the upstream connection reaches the echo server instead of the fake upstream.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* typed_config = bootstrap.mutable_bootstrap_extensions(0)->mutable_typed_config();
      DynamicModuleSocketInterface proto_config;
      ASSERT_TRUE(typed_config->UnpackTo(&proto_config));
      Protobuf::StringValue dial_port;
      dial_port.set_value(absl::StrCat(fake_upstreams_[0]->localAddress()->ip()->port()));
      ASSERT_TRUE(proto_config.mutable_socket_interface_config()->PackFrom(dial_port));
      ASSERT_TRUE(typed_config->PackFrom(proto_config));
    });
  }

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcp_proxy
            cluster: cluster_0
default_socket_interface: "envoy.extensions.network.socket_interface.dynamic_modules"
    )EOF");
  }
};

TEST_F(DynamicModuleSocketInterfaceIntegrationTest, RequestFlowsThroughModuleSocket) {
  BaseIntegrationTest::initialize();

  const Network::SocketInterface* factory =
      Network::socketInterface("envoy.extensions.network.socket_interface.dynamic_modules");
  ASSERT_TRUE(Network::SocketInterfaceSingleton::getExisting() == factory);

  std::string response;
  auto connection = createConnectionDriver(
      lookupPort("listener_0"), "hello",
      [&response](Network::ClientConnection& conn, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        conn.close(Network::ConnectionCloseType::FlushWrite);
      });
  ASSERT_TRUE(connection->run());
  EXPECT_EQ("hello", response);
}

} // namespace
} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
