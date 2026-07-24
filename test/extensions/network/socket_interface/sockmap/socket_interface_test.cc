#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/extensions/network/socket_interface/sockmap/v3/sockmap.pb.h"

#include "source/extensions/network/socket_interface/sockmap/io_handle.h"
#include "source/extensions/network/socket_interface/sockmap/socket_interface.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Network {
namespace {

class NoopDatapath : public BpfDatapath {
public:
  void registerSocket(os_fd_t, const Address::Instance&, const Address::Instance&) override {}
  void unregisterSocket(const Address::Instance&, const Address::Instance&) override {}
};

// Intercepts createDatapath so the tests can observe the parsed config without loading eBPF
// programs, and installs a bootstrap extension so makeSocket can be driven directly.
class SockmapSocketInterfacePeer : public SockmapSocketInterface {
public:
  using SockmapSocketInterface::makeSocket;
  // Installs an extension carrying the given datapath and policy, mirroring what
  // createBootstrapExtension does at startup so makeSocket sees the state.
  void installExtension(BpfDatapathSharedPtr datapath, bool register_user_space_sockets) {
    extension_holder_ = std::make_unique<SockmapSocketInterfaceExtension>(
        *this, std::move(datapath), register_user_space_sockets);
  }
  const BpfDatapathConfig& lastConfig() const { return last_config_; }

  // Datapath returned from createDatapath so config parsing can be tested without eBPF.
  BpfDatapathSharedPtr injected_datapath_;

protected:
  BpfDatapathSharedPtr createDatapath(const BpfDatapathConfig& config) override {
    last_config_ = config;
    return injected_datapath_;
  }

private:
  BpfDatapathConfig last_config_;
  Server::BootstrapExtensionPtr extension_holder_;
};

TEST(SockmapSocketInterface, Name) {
  SockmapSocketInterface interface;
  EXPECT_EQ(interface.name(), "envoy.extensions.network.socket_interface.sockmap");
}

TEST(SockmapSocketInterface, CreateEmptyConfigProto) {
  SockmapSocketInterface interface;
  ProtobufTypes::MessagePtr proto = interface.createEmptyConfigProto();
  ASSERT_NE(proto, nullptr);
  EXPECT_NE(dynamic_cast<envoy::extensions::network::socket_interface::sockmap::v3::Sockmap*>(
                proto.get()),
            nullptr);
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionAppliesDefaults) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;

  envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
  Server::BootstrapExtensionPtr extension = interface.createBootstrapExtension(config, context);
  ASSERT_NE(extension, nullptr);
  auto* sockmap_extension = dynamic_cast<SockmapSocketInterfaceExtension*>(extension.get());
  ASSERT_NE(sockmap_extension, nullptr);

  EXPECT_TRUE(sockmap_extension->registerUserSpaceSockets());
  EXPECT_EQ(interface.lastConfig().sockhash_max_entries, 65536U);
  EXPECT_EQ(interface.lastConfig().bpf_program_path, "");
  EXPECT_EQ(interface.lastConfig().cgroup_path, "");
  EXPECT_TRUE(interface.lastConfig().accelerated_ports.empty());
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionParsesExplicitValues) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;
  interface.injected_datapath_ = std::make_shared<NoopDatapath>();

  envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
  config.set_bpf_program_path("/tmp/sockmap.o");
  config.set_cgroup_path("/sys/fs/cgroup/envoy");
  config.mutable_sockhash_max_entries()->set_value(1024);
  config.mutable_register_user_space_sockets()->set_value(true);
  auto* single_port = config.add_accelerated_ports();
  single_port->set_start(9211);
  single_port->set_end(9212);
  auto* range = config.add_accelerated_ports();
  range->set_start(6000);
  range->set_end(6010);

  Server::BootstrapExtensionPtr extension = interface.createBootstrapExtension(config, context);
  ASSERT_NE(extension, nullptr);
  auto* sockmap_extension = dynamic_cast<SockmapSocketInterfaceExtension*>(extension.get());
  ASSERT_NE(sockmap_extension, nullptr);

  EXPECT_EQ(interface.lastConfig().bpf_program_path, "/tmp/sockmap.o");
  EXPECT_EQ(interface.lastConfig().cgroup_path, "/sys/fs/cgroup/envoy");
  EXPECT_EQ(interface.lastConfig().sockhash_max_entries, 1024U);
  ASSERT_EQ(interface.lastConfig().accelerated_ports.size(), 2U);
  EXPECT_EQ(interface.lastConfig().accelerated_ports[0].start, 9211U);
  EXPECT_EQ(interface.lastConfig().accelerated_ports[0].end, 9212U);
  EXPECT_EQ(interface.lastConfig().accelerated_ports[1].start, 6000U);
  EXPECT_EQ(interface.lastConfig().accelerated_ports[1].end, 6010U);
  EXPECT_TRUE(sockmap_extension->registerUserSpaceSockets());

  // With a datapath loaded and registration enabled, IPv4 stream sockets use the accelerated
  // handle. The returned extension stays alive, so the interface back pointer is valid here.
  IoHandlePtr handle = interface.makeSocket(-1, false, Socket::Type::Stream, AF_INET, {});
  EXPECT_NE(dynamic_cast<SockmapIoSocketHandle*>(handle.get()), nullptr);
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionRejectsZeroMaxEntries) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;

  envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
  config.mutable_sockhash_max_entries()->set_value(0);

  EXPECT_THROW(interface.createBootstrapExtension(config, context), EnvoyException);
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionRejectsInvalidPortRange) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;

  // start below 1.
  {
    envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
    auto* range = config.add_accelerated_ports();
    range->set_start(0);
    range->set_end(10);
    EXPECT_THROW(interface.createBootstrapExtension(config, context), EnvoyException);
  }
  // end above 65536.
  {
    envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
    auto* range = config.add_accelerated_ports();
    range->set_start(1);
    range->set_end(65537);
    EXPECT_THROW(interface.createBootstrapExtension(config, context), EnvoyException);
  }
  // start not less than end.
  {
    envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
    auto* range = config.add_accelerated_ports();
    range->set_start(9211);
    range->set_end(9211);
    EXPECT_THROW(interface.createBootstrapExtension(config, context), EnvoyException);
  }
  // Every range is validated, not just the first.
  {
    envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
    auto* valid = config.add_accelerated_ports();
    valid->set_start(9211);
    valid->set_end(9212);
    auto* invalid = config.add_accelerated_ports();
    invalid->set_start(5);
    invalid->set_end(5);
    EXPECT_THROW(interface.createBootstrapExtension(config, context), EnvoyException);
  }
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionRejectsTooManyRanges) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;

  envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
  for (uint32_t port = 1; port <= 129; ++port) {
    auto* range = config.add_accelerated_ports();
    range->set_start(port);
    range->set_end(port + 1);
  }

  EXPECT_THROW(interface.createBootstrapExtension(config, context), EnvoyException);
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionAcceptsFullPortRange) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;

  envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
  auto* range = config.add_accelerated_ports();
  range->set_start(1);
  range->set_end(65536);

  Server::BootstrapExtensionPtr extension = interface.createBootstrapExtension(config, context);
  ASSERT_NE(extension, nullptr);
  ASSERT_EQ(interface.lastConfig().accelerated_ports.size(), 1U);
  EXPECT_EQ(interface.lastConfig().accelerated_ports[0].start, 1U);
  EXPECT_EQ(interface.lastConfig().accelerated_ports[0].end, 65536U);
}

TEST(SockmapSocketInterface, CreateBootstrapExtensionDisablesRegistration) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SockmapSocketInterfacePeer interface;
  interface.injected_datapath_ = std::make_shared<NoopDatapath>();

  envoy::extensions::network::socket_interface::sockmap::v3::Sockmap config;
  config.mutable_register_user_space_sockets()->set_value(false);

  Server::BootstrapExtensionPtr extension = interface.createBootstrapExtension(config, context);
  ASSERT_NE(extension, nullptr);
  auto* sockmap_extension = dynamic_cast<SockmapSocketInterfaceExtension*>(extension.get());
  ASSERT_NE(sockmap_extension, nullptr);
  EXPECT_FALSE(sockmap_extension->registerUserSpaceSockets());

  // Even with a datapath loaded, disabling registration keeps IPv4 stream sockets on the standard
  // handle.
  IoHandlePtr handle = interface.makeSocket(-1, false, Socket::Type::Stream, AF_INET, {});
  EXPECT_EQ(dynamic_cast<SockmapIoSocketHandle*>(handle.get()), nullptr);
}

TEST(SockmapSocketInterface, MakeSocketUsesStandardHandleForDatagram) {
  SockmapSocketInterfacePeer interface;
  interface.installExtension(std::make_shared<NoopDatapath>(), true);

  IoHandlePtr handle = interface.makeSocket(-1, false, Socket::Type::Datagram, AF_INET, {});
  EXPECT_EQ(dynamic_cast<SockmapIoSocketHandle*>(handle.get()), nullptr);
}

TEST(SockmapSocketInterface, MakeSocketUsesStandardHandleForNonInetDomain) {
  SockmapSocketInterfacePeer interface;
  interface.installExtension(std::make_shared<NoopDatapath>(), true);

  IoHandlePtr handle = interface.makeSocket(-1, false, Socket::Type::Stream, AF_INET6, {});
  EXPECT_EQ(dynamic_cast<SockmapIoSocketHandle*>(handle.get()), nullptr);
}

TEST(SockmapSocketInterface, MakeSocketUsesStandardHandleWithoutDatapath) {
  SockmapSocketInterfacePeer interface;

  IoHandlePtr handle = interface.makeSocket(-1, false, Socket::Type::Stream, AF_INET, {});
  EXPECT_EQ(dynamic_cast<SockmapIoSocketHandle*>(handle.get()), nullptr);
}

} // namespace
} // namespace Network
} // namespace Envoy
