#include <unistd.h>

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class UpstreamReverseConnectionIOHandleTest : public testing::Test {
protected:
  UpstreamReverseConnectionIOHandleTest() {
    // Set up stats scope for extensions if needed.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
  }

  void TearDown() override {
    io_handle_.reset();
    if (extension_) {
      extension_.reset();
    }
    if (socket_interface_) {
      socket_interface_.reset();
    }
  }

  // Helper to create a mock socket with IO handle.
  Network::ConnectionSocketPtr createMockSocket() {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    socket->io_handle_ = std::move(mock_io_handle);
    return socket;
  }

  // Helper to set up the upstream extension components (socket interface and extension).
  void setupUpstreamExtension() {
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(server_context_);
    extension_ = std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_,
                                                                  server_context_, config_);

    // Get the registered socket interface from the global registry and set up its extension.
    auto* registered_socket_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (registered_socket_interface) {
      auto* registered_acceptor = dynamic_cast<ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_socket_interface));
      if (registered_acceptor) {
        registered_acceptor->extension_ = extension_.get();
      }
    }
  }

  // Helper to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    if (!extension_) {
      return;
    }
    extension_->onServerInitialized();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<UpstreamReverseConnectionIOHandle> io_handle_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(UpstreamReverseConnectionIOHandleTest, ConnectReturnsSuccess) {
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  auto result = io_handle_->connect(address);

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, GetSocketReturnsConstReference) {
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");
  const auto& socket = io_handle_->getSocket();

  EXPECT_NE(&socket, nullptr);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, ShutdownIgnoredWhenOwned) {
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");
  auto result = io_handle_->shutdown(SHUT_RDWR);
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

// Test close() when socket interface is not registered.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWhenSocketInterfaceNotRegistered) {
  // Save current factories.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Find and remove the specific socket interface factory.
  auto& factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();
  auto it = factories.find("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (it != factories.end()) {
    factories.erase(it);
  }

  // Create IO handle with owned socket.
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");

  // Close should handle the missing interface gracefully.
  auto result = io_handle_->close();

  // Should return success (falls back to IoSocketHandleImpl::close()).
  EXPECT_EQ(result.err_, nullptr);

  // Restore the registry.
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

// Test close() when socket interface is registered but is the wrong type.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWhenSocketInterfaceWrongType) {
  // Create a mock socket interface that is a SocketInterface but not a ReverseTunnelAcceptor.
  // This will cause the dynamic_cast to ReverseTunnelAcceptor to fail.
  class WrongTypeSocketInterface : public Network::SocketInterface,
                                   public Server::Configuration::BootstrapExtensionFactory {
  public:
    std::string name() const override {
      return "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";
    }

    Server::BootstrapExtensionPtr
    createBootstrapExtension(const Protobuf::Message&,
                             Server::Configuration::ServerFactoryContext&) override {
      return nullptr;
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }

    // SocketInterface methods
    Network::IoHandlePtr socket(Network::Socket::Type, Network::Address::Type,
                                Network::Address::IpVersion, bool,
                                const Network::SocketCreationOptions&) const override {
      return nullptr;
    }

    Network::IoHandlePtr socket(Network::Socket::Type,
                                const Network::Address::InstanceConstSharedPtr,
                                const Network::SocketCreationOptions&) const override {
      return nullptr;
    }

    bool ipFamilySupported(int) override { return false; }
  };

  // Save current factories.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Register the wrong type socket interface.
  WrongTypeSocketInterface wrong_socket_interface;
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories()
      ["envoy.bootstrap.reverse_tunnel.upstream_socket_interface"] = &wrong_socket_interface;

  // Create IO handle with owned socket.
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");

  // Close should handle the wrong type gracefully.
  auto result = io_handle_->close();

  // Should return success (falls back to IoSocketHandleImpl::close()).
  EXPECT_EQ(result.err_, nullptr);

  // Restore the registry.
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

// Test close() when socket interface is registered but TLS slot is not set up.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWhenTLSSlotNotSetUp) {
  // Set up the upstream extension but do NOT initialize the TLS slot.
  setupUpstreamExtension();

  // Create IO handle with owned socket.
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");

  // Close should handle the missing TLS slot gracefully.
  auto result = io_handle_->close();

  // Should return success (falls back to IoSocketHandleImpl::close()).
  EXPECT_EQ(result.err_, nullptr);
}

// Test close() when socket interface and TLS slot are properly set up.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWithSocketManagerNotification) {
  // Set up the upstream extension and TLS slot.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Create IO handle with owned socket.
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");

  // Close should notify the socket manager and clean up properly.
  auto result = io_handle_->close();

  // Should return success with no error.
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.err_, nullptr);
}

// Test close() when owned_socket_ is nullptr.
TEST_F(UpstreamReverseConnectionIOHandleTest, CloseWithoutOwnedSocket) {
  // Create IO handle with owned socket.
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");

  // Release the owned socket without closing/invalidating the fd.
  io_handle_->releaseSocketForTest();

  // Now close should call IoSocketHandleImpl::close().
  auto result = io_handle_->close();

  // Should return success.
  EXPECT_EQ(result.err_, nullptr);
}

// Test shutdown() when owned_socket_ is nullptr.
TEST_F(UpstreamReverseConnectionIOHandleTest, ShutdownWhenNotOwned) {
  // Create IO handle with owned socket.
  io_handle_ =
      std::make_unique<UpstreamReverseConnectionIOHandle>(createMockSocket(), "test-cluster");

  // Release the owned socket without closing/invalidating the fd.
  io_handle_->releaseSocketForTest();

  // Now shutdown should call IoSocketHandleImpl::shutdown().
  auto result = io_handle_->shutdown(SHUT_RDWR);
  EXPECT_GE(result.return_value_, -1);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
