#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"
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

using DynamicModuleSocketInterfaceProto =
    envoy::extensions::network::socket_interface::dynamic_modules::v3::DynamicModuleSocketInterface;

class DynamicModuleSocketInterfaceTest : public testing::Test {
public:
  DynamicModuleSocketInterfaceTest() : api_(Api::createApiForTest()) {}

  void SetUp() override { dispatcher_ = api_->allocateDispatcher("test"); }

  // Loads the module and installs it as the extension of the interface under test. The module is
  // loaded once more with do_not_close so the test helpers it exports stay callable through the
  // same dlopen handle the extension uses.
  void installInterface(const std::string& module_name) {
    const std::string path = Extensions::DynamicModules::testSharedObjectPath(module_name, "c");
    module_ = Extensions::DynamicModules::newDynamicModule(path, /*do_not_close=*/true);
    ASSERT_TRUE(module_.ok()) << module_.status().message();

    DynamicModuleSocketInterfaceProto proto_config;
    proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
        path);
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_socket_interface_name("socket_interface_test");
    auto result = interface_.createBootstrapExtension(proto_config, context_);
    ASSERT_TRUE(result.ok()) << result.status().message();
    extension_ = std::move(result.value());
  }

  // Resolves a helper exported by the loaded module.
  template <class T> T getModuleFunction(const std::string& name) {
    auto function = module_.value()->getFunctionPointer<T>(name);
    EXPECT_TRUE(function.ok()) << function.status().message();
    return function.value();
  }

  Network::IoHandlePtr
  makeSocket(uint32_t port, Network::Socket::Type socket_type = Network::Socket::Type::Stream) {
    auto addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", port);
    Network::SocketCreationOptions options;
    return interface_.socket(socket_type, addr, options);
  }

  Network::IoHandlePtr makeSocketV6() {
    Network::SocketCreationOptions options;
    return interface_.socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                             Network::Address::IpVersion::v6, false, options);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  DynamicModuleSocketInterface interface_;
  Server::BootstrapExtensionPtr extension_;
  absl::StatusOr<Extensions::DynamicModules::DynamicModulePtr> module_;
};

TEST_F(DynamicModuleSocketInterfaceTest, DelegatesDataPathAndSkipsConnect) {
  installInterface("socket_interface_test");

  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle.get());
  auto* module_handle = dynamic_cast<DynamicModuleIoHandle*>(handle.get());
  ASSERT_NE(nullptr, module_handle);

  // The module handed Envoy an already-connected socket.
  auto connect_result =
      handle->connect(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1000));
  EXPECT_EQ(0, connect_result.return_value_);
  EXPECT_EQ(0, connect_result.errno_);

  // A write through the handle reaches the module's peer socket.
  Buffer::OwnedImpl buffer("hello");
  auto write_result = handle->write(buffer);
  ASSERT_TRUE(write_result.ok());
  EXPECT_EQ(5, write_result.return_value_);
  char received[8] = {0};
  const int peer_fd = getModuleFunction<int (*)(void)>("getSocketInterfaceTestPeerFd")();
  ASSERT_GE(peer_fd, 0);
  EXPECT_EQ(5, ::recv(peer_fd, received, sizeof(received), 0));
  EXPECT_EQ("hello", std::string(received, 5));

  // A read through the handle picks up what the peer socket sent.
  ASSERT_EQ(3, ::send(peer_fd, "abc", 3, 0));
  char sent[4] = {0};
  Buffer::RawSlice slice{sent, sizeof(sent)};
  auto read_result = handle->readv(sizeof(sent), &slice, 1);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ(3, read_result.return_value_);
  EXPECT_EQ("abc", std::string(sent, 3));

  // With nothing in flight, the read reports EAGAIN.
  Buffer::RawSlice empty_slice{sent, sizeof(sent)};
  auto again_result = handle->readv(sizeof(sent), &empty_slice, 1);
  EXPECT_TRUE(again_result.wouldBlock());

  // The close counter is module-global and the module stays loaded across tests, so assert on
  // the change rather than the absolute value.
  const int initial_close_count =
      getModuleFunction<int (*)(void)>("getSocketInterfaceTestCloseCount")();
  EXPECT_TRUE(handle->close().ok());
  EXPECT_EQ(initial_close_count + 1,
            getModuleFunction<int (*)(void)>("getSocketInterfaceTestCloseCount")());
  // Closing twice does not release the module state twice.
  EXPECT_TRUE(handle->close().ok());
  EXPECT_EQ(initial_close_count + 1,
            getModuleFunction<int (*)(void)>("getSocketInterfaceTestCloseCount")());
}

TEST_F(DynamicModuleSocketInterfaceTest, DestructorReleasesModuleState) {
  installInterface("socket_interface_test");
  const int initial_close_count =
      getModuleFunction<int (*)(void)>("getSocketInterfaceTestCloseCount")();
  {
    auto handle = makeSocket(1000);
    ASSERT_NE(nullptr, handle.get());
  }
  // Destroying the handle without closing it still releases the module state.
  EXPECT_EQ(initial_close_count + 1,
            getModuleFunction<int (*)(void)>("getSocketInterfaceTestCloseCount")());
}

TEST_F(DynamicModuleSocketInterfaceTest, ShutdownDelegatesToModule) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle.get());

  auto result = handle->shutdown(SHUT_WR);
  EXPECT_EQ(0, result.return_value_);
  EXPECT_EQ(0, result.errno_);

  const int peer_fd = getModuleFunction<int (*)(void)>("getSocketInterfaceTestPeerFd")();
  char received[8];
  EXPECT_EQ(0, ::recv(peer_fd, received, sizeof(received), 0));
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, IoReadyWakesTheFileEvent) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle.get());

  // Registered with no kernel interest, so any ready event comes from the module.
  uint32_t fired_events = 0;
  handle->initializeFileEvent(
      *dispatcher_,
      [&fired_events](uint32_t events) -> absl::Status {
        fired_events |= events;
        return absl::OkStatus();
      },
      Event::FileTriggerType::Level, 0);

  auto trigger = getModuleFunction<void (*)(uint32_t)>("triggerSocketInterfaceTestReady");
  trigger(envoy_dynamic_module_type_socket_interface_io_event_Read);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Event::FileReadyType::Read, fired_events);

  trigger(envoy_dynamic_module_type_socket_interface_io_event_Read |
          envoy_dynamic_module_type_socket_interface_io_event_Write);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Event::FileReadyType::Read | Event::FileReadyType::Write, fired_events);

  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, CreationOnlyModuleUsesStandardHandle) {
  installInterface("socket_interface_creation_only");

  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle.get());
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));

  EXPECT_TRUE(interface_.ipFamilySupported(AF_INET));
  EXPECT_FALSE(interface_.ipFamilySupported(AF_INET6));

  // The module does not support IPv6, so the standard interface creates that socket instead.
  auto v6_handle = makeSocketV6();
  EXPECT_NE(nullptr, v6_handle.get());
  EXPECT_TRUE(handle->close().ok());
  EXPECT_TRUE(v6_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, DatagramsFallBackToTheStandardInterface) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000, Network::Socket::Type::Datagram);
  ASSERT_NE(nullptr, handle.get());
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));
  EXPECT_TRUE(handle->close().ok());
}

TEST(DynamicModuleSocketInterfaceFallbackTest, WithoutExtensionUsesStandardHandle) {
  DynamicModuleSocketInterface interface;
  auto addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1000);
  Network::SocketCreationOptions options;
  auto handle = interface.socket(Network::Socket::Type::Stream, addr, options);
  EXPECT_NE(nullptr, handle.get());
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));
  EXPECT_TRUE(handle->close().ok());
}

} // namespace
} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
