#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/fake_upstream.h"
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

// default_socket_interface installs a process-wide singleton that outlives the server, so the
// previous test's interface would otherwise create this server's admin and fake upstream sockets.
void useStandardSocketInterface() {
  auto* standard = const_cast<Network::SocketInterface*>(Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface"));
  RELEASE_ASSERT(standard != nullptr, "the standard socket interface is not registered");
  Network::SocketInterfaceSingleton::replaceForTest(standard);
}

// The module dials its own echo server for sockets to the configured port and hands Envoy the
// already-connected socket, so the upstream leg of a request runs through the module's I/O hooks.
class DynamicModuleSocketInterfaceIntegrationTest : public BaseIntegrationTest,
                                                    public testing::Test {
public:
  DynamicModuleSocketInterfaceIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, config()) {
    useStandardSocketInterface();
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
    // The fake upstream's port is final here, and setPorts points the cluster at it, so the
    // module answers the upstream connection with its echo server.
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

// The module creates every socket itself, setting TCP_NODELAY, and leaves all I/O to Envoy, so a
// request runs through module-created listener and upstream sockets.
class DynamicModuleSocketInterfaceCreationOnlyIntegrationTest : public BaseIntegrationTest,
                                                                public testing::Test {
public:
  DynamicModuleSocketInterfaceCreationOnlyIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, config()) {
    useStandardSocketInterface();
    use_lds_ = false;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* extension = bootstrap.mutable_bootstrap_extensions()->Add();
      extension->set_name("envoy.extensions.network.socket_interface.dynamic_modules");
      DynamicModuleSocketInterface proto_config;
      proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
          Extensions::DynamicModules::testSharedObjectPath("socket_interface_creation_only", "c"));
      proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
      proto_config.set_socket_interface_name("creation_only");
      ASSERT_TRUE(extension->mutable_typed_config()->PackFrom(proto_config));
    });
  }

  static std::string config() { return DynamicModuleSocketInterfaceIntegrationTest::config(); }
};

TEST_F(DynamicModuleSocketInterfaceCreationOnlyIntegrationTest, ModuleCreatedSocketsServeTraffic) {
  BaseIntegrationTest::initialize();

  auto client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  ASSERT_TRUE(client->write("hello"));
  ASSERT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));
  ASSERT_TRUE(upstream_connection->write("world"));
  client->waitForData("world");

  client->close();
  ASSERT_TRUE(upstream_connection->close());
}

// Two workers without reuse_port, so the second worker's listen socket is a copy. Each handed-over
// connection serves "hello" from the module's memory over a socket the kernel never signals,
// reported before Envoy registers it, and runs through tcp_proxy to the upstream.
class DynamicModuleSocketInterfaceAcceptorIntegrationTest : public BaseIntegrationTest,
                                                            public testing::Test {
public:
  DynamicModuleSocketInterfaceAcceptorIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, config()) {
    useStandardSocketInterface();
    use_lds_ = false;
    concurrency_ = 2;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* extension = bootstrap.mutable_bootstrap_extensions()->Add();
      extension->set_name("envoy.extensions.network.socket_interface.dynamic_modules");
      DynamicModuleSocketInterface proto_config;
      proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
          Extensions::DynamicModules::testSharedObjectPath("socket_interface_test", "rust"));
      proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
      proto_config.set_socket_interface_name("socket_interface_test");
      Protobuf::StringValue role;
      role.set_value("acceptor");
      ASSERT_TRUE(proto_config.mutable_socket_interface_config()->PackFrom(role));
      ASSERT_TRUE(extension->mutable_typed_config()->PackFrom(proto_config));
      bootstrap.mutable_static_resources()
          ->mutable_listeners(0)
          ->mutable_enable_reuse_port()
          ->set_value(false);
    });
  }

  static std::string config() { return DynamicModuleSocketInterfaceIntegrationTest::config(); }

  // Resolves a helper the Rust module exports, through the handle Envoy loaded it with.
  template <class T> T moduleFunction(const std::string& name) {
    if (!module_.ok() || module_.value() == nullptr) {
      module_ = Extensions::DynamicModules::newDynamicModule(
          Extensions::DynamicModules::testSharedObjectPath("socket_interface_test", "rust"),
          /*do_not_close=*/true);
    }
    EXPECT_TRUE(module_.ok()) << module_.status().message();
    auto function = module_.value()->getFunctionPointer<T>(name);
    EXPECT_TRUE(function.ok()) << function.status().message();
    return function.value();
  }

  absl::StatusOr<Extensions::DynamicModules::DynamicModulePtr> module_{
      absl::UnknownError("not loaded")};
};

TEST_F(DynamicModuleSocketInterfaceAcceptorIntegrationTest,
       HandedOverConnectionsRunThroughTheFilterChain) {
  BaseIntegrationTest::initialize();
  // The second worker's listen socket is a copy with its own module state.
  EXPECT_GE(moduleFunction<uint32_t (*)(void)>("socket_interface_test_duplicate_count")(), 1);

  // Nothing reaches the listen sockets until the module reports a connection, which it does from
  // a thread of its own.
  auto queue_connection = moduleFunction<void (*)(void)>("socket_interface_test_queue_connection");
  for (int i = 0; i < 2; i++) {
    queue_connection();
    FakeRawConnectionPtr upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));
    ASSERT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));
    ASSERT_TRUE(upstream_connection->close());
  }
}

} // namespace
} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
