#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

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

class TestReverseTunnelAcceptor : public testing::Test {
protected:
  TestReverseTunnelAcceptor() {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    config_.set_stat_prefix("test_prefix");
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);

    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    socket_manager_ = std::make_unique<UpstreamSocketManager>(dispatcher_, extension_.get());
  }

  void TearDown() override {
    socket_manager_.reset();
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  void setupThreadLocalSlot() {
    extension_->onServerInitialized();
    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());
    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    socket->io_handle_ = std::move(mock_io_handle);
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  Network::Address::InstanceConstSharedPtr
  createAddressWithLogicalName(const std::string& logical_name) {
    class TestAddress : public Network::Address::Instance {
    public:
      TestAddress(const std::string& logical_name) : logical_name_(logical_name) {
        address_string_ = "127.0.0.1:8080";
      }

      bool operator==(const Instance& rhs) const override {
        return logical_name_ == rhs.logicalName();
      }
      Network::Address::Type type() const override { return Network::Address::Type::Ip; }
      const std::string& asString() const override { return address_string_; }
      absl::string_view asStringView() const override { return address_string_; }
      const std::string& logicalName() const override { return logical_name_; }
      const Network::Address::Ip* ip() const override { return nullptr; }
      const Network::Address::Pipe* pipe() const override { return nullptr; }
      const Network::Address::EnvoyInternalAddress* envoyInternalAddress() const override {
        return nullptr;
      }
      const sockaddr* sockAddr() const override { return nullptr; }
      socklen_t sockAddrLen() const override { return 0; }
      absl::string_view addressType() const override { return "test"; }
      absl::optional<std::string> networkNamespace() const override { return absl::nullopt; }
      Network::Address::InstanceConstSharedPtr
      withNetworkNamespace(absl::string_view) const override {
        return nullptr;
      }
      const Network::SocketInterface& socketInterface() const override {
        return Network::SocketInterfaceSingleton::get();
      }

    private:
      std::string logical_name_;
      std::string address_string_;
    };

    return std::make_shared<TestAddress>(logical_name);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::unique_ptr<UpstreamSocketManager> socket_manager_;

  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;
};

TEST_F(TestReverseTunnelAcceptor, GetLocalRegistryNoExtension) {
  auto* registry = socket_interface_->getLocalRegistry();
  EXPECT_EQ(registry, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, GetLocalRegistryWithExtension) {
  setupThreadLocalSlot();

  auto* registry = socket_interface_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);
  EXPECT_EQ(registry, thread_local_registry_.get());
}

TEST_F(TestReverseTunnelAcceptor, CreateBootstrapExtension) {
  auto extension = socket_interface_->createBootstrapExtension(config_, context_);
  EXPECT_NE(extension, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, CreateEmptyConfigProto) {
  auto config = socket_interface_->createEmptyConfigProto();
  EXPECT_NE(config, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithoutAddress) {
  Network::SocketCreationOptions options;
  auto io_handle =
      socket_interface_->socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                                Network::Address::IpVersion::v4, false, options);
  EXPECT_EQ(io_handle, nullptr);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithAddressNoThreadLocal) {
  const std::string node_id = "test-node";
  auto address = createAddressWithLogicalName(node_id);
  Network::SocketCreationOptions options;
  auto io_handle = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(io_handle, nullptr);
  EXPECT_EQ(dynamic_cast<UpstreamReverseConnectionIOHandle*>(io_handle.get()), nullptr);

  // Verify fallback counter increments for diagnostics.
  // Counter name is "<scope>.<stat_prefix>.fallback_no_reverse_socket".
  auto& scope = extension_->getStatsScope();
  std::string counter_name = absl::StrCat(extension_->statPrefix(), ".fallback_no_reverse_socket");
  Stats::StatNameManagedStorage counter_name_storage(counter_name, scope.symbolTable());
  auto& counter = scope.counterFromStatName(counter_name_storage.statName());
  EXPECT_EQ(counter.value(), 1);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithAddressAndThreadLocalNoCachedSockets) {
  setupThreadLocalSlot();

  const std::string node_id = "test-node";
  auto address = createAddressWithLogicalName(node_id);

  Network::SocketCreationOptions options;
  auto io_handle = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(io_handle, nullptr);
  EXPECT_EQ(dynamic_cast<UpstreamReverseConnectionIOHandle*>(io_handle.get()), nullptr);
}

TEST_F(TestReverseTunnelAcceptor, SocketWithAddressAndThreadLocalWithCachedSockets) {
  setupThreadLocalSlot();

  auto* tls_socket_manager = socket_interface_->getLocalRegistry()->socketManager();
  EXPECT_NE(tls_socket_manager, nullptr);

  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  tls_socket_manager->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                          false);

  auto address = createAddressWithLogicalName(node_id);

  Network::SocketCreationOptions options;
  auto io_handle = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(io_handle, nullptr);

  auto* upstream_io_handle = dynamic_cast<UpstreamReverseConnectionIOHandle*>(io_handle.get());
  EXPECT_NE(upstream_io_handle, nullptr);

  auto another_io_handle =
      socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(another_io_handle, nullptr);
  EXPECT_EQ(dynamic_cast<UpstreamReverseConnectionIOHandle*>(another_io_handle.get()), nullptr);
}

TEST_F(TestReverseTunnelAcceptor, IpFamilySupported) {
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET));
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET6));
  EXPECT_FALSE(socket_interface_->ipFamilySupported(AF_UNIX));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
