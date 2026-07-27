#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/http/headers.h"
#include "source/common/network/socket_interface.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/singleton/threadsafe_singleton.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/clusters/reverse_connection/reverse_connection.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {
class TestLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection)
      : TestLoadBalancerContext(connection, nullptr) {}
  TestLoadBalancerContext(const Network::Connection* connection,
                          StreamInfo::StreamInfo* request_stream_info)
      : connection_(connection), request_stream_info_(request_stream_info) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : TestLoadBalancerContext(connection) {
    downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{key, value}}};
  }

  // Upstream::LoadBalancerContext.
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  StreamInfo::StreamInfo* requestStreamInfo() const override { return request_stream_info_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  absl::optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
  StreamInfo::StreamInfo* request_stream_info_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class ReverseConnectionClusterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ReverseConnectionClusterTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Allow timers and file events to be created multiple times during tests.
    ON_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillByDefault(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    ON_CALL(server_context_.dispatcher_, createFileEvent_(_, _, _, _))
        .WillByDefault(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    // Create the config.
    config_.set_stat_prefix("test_prefix");
  }

  ~ReverseConnectionClusterTest() override = default;

  // Set up the upstream extension components (socket interface and extension).
  void setupUpstreamExtension() {
    // Create the socket interface.
    socket_interface_ =
        std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptor>(server_context_);

    // Create the extension.
    extension_ = std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptorExtension>(
        *socket_interface_, server_context_, config_);

    // Get the registered socket interface from the global registry and set up its extension.
    auto* registered_socket_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (registered_socket_interface) {
      auto* registered_acceptor = dynamic_cast<BootstrapReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_socket_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered socket interface.
        registered_acceptor->extension_ = extension_.get();
      }
    }
  }

  void setupFromYaml(const std::string& yaml, bool expect_success = true) {
    if (expect_success) {
      cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
      EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
      EXPECT_CALL(*cleanup_timer_, disableTimer()).Times(testing::AnyNumber());
      EXPECT_CALL(initialized_, ready());
    }
    setup(Upstream::parseClusterFromV3Yaml(yaml));
  }

  void setup(const envoy::config::cluster::v3::Cluster& cluster_config) {

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    RevConClusterFactory factory;

    // Parse the ReverseConnectionClusterConfig from the cluster's typed_config.
    envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig
        rev_con_config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), validation_visitor_, rev_con_config));

    auto status_or_pair =
        factory.createClusterWithConfig(cluster_config, rev_con_config, factory_context);
    THROW_IF_NOT_OK_REF(status_or_pair.status());

    cluster_ = std::dynamic_pointer_cast<RevConCluster>(status_or_pair.value().first);
    priority_update_cb_ = cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) {
          membership_updated_.ready();
          return absl::OkStatus();
        });
    ON_CALL(initialized_, ready()).WillByDefault(testing::Invoke([this] {
      init_complete_ = true;
    }));
    cluster_->initialize([&]() {
      initialized_.ready();
      return absl::OkStatus();
    });
  }

  void TearDown() override {
    // Do not assert on timer teardown; allow destructor-time disable.

    // Clean up thread local resources if they were set up.
    if (tls_slot_) {
      tls_slot_.reset();
    }
    // Don't reset thread_local_registry_ as it's owned by the extension.
    if (extension_) {
      extension_.reset();
    }
    if (socket_interface_) {
      socket_interface_.reset();
    }
  }

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // Check if extension is set up.
    if (!extension_) {
      return;
    }

    // Let the extension create and own its TLS slot and manager to avoid duplicate timer/file
    // event creation.
    extension_->onServerInitialized();
  }

  // Helper to add a socket to the manager for testing.
  void addTestSocket(const std::string& node_id, const std::string& cluster_id) {
    if (!socket_interface_) {
      return;
    }

    auto* local_registry = socket_interface_->getLocalRegistry();
    if (local_registry == nullptr || local_registry->socketManager() == nullptr) {
      return;
    }

    // Create a mock socket. Timer and file event creation will use the ON_CALL defaults.
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    socket->io_handle_ = std::move(mock_io_handle);

    // Get the socket manager from the thread local registry.
    auto* tls_socket_manager = local_registry->socketManager();
    EXPECT_NE(tls_socket_manager, nullptr);

    // Add the socket to the manager.
    tls_socket_manager->addConnectionSocket(node_id, cluster_id, std::move(socket),
                                            std::chrono::seconds(30), false);
  }

  // Helper method to call cleanup since this class is a friend of RevConCluster.
  void callCleanup() { cluster_->cleanup(); }

  // Helper method to create LoadBalancerFactory instance for testing.
  std::unique_ptr<RevConCluster::LoadBalancerFactory> createLoadBalancerFactory() {
    return std::make_unique<RevConCluster::LoadBalancerFactory>(cluster_);
  }

  // Helper method to create ThreadAwareLoadBalancer instance for testing.
  std::unique_ptr<RevConCluster::ThreadAwareLoadBalancer> createThreadAwareLoadBalancer() {
    return std::make_unique<RevConCluster::ThreadAwareLoadBalancer>(cluster_);
  }

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  std::shared_ptr<RevConCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  Event::MockTimer* cleanup_timer_;
  ::Envoy::Common::CallbackHandlePtr priority_update_cb_;
  bool init_complete_{false};

  // Real thread local slot and registry for reverse connection testing.
  std::unique_ptr<ThreadLocal::TypedSlot<BootstrapReverseConnection::UpstreamSocketThreadLocal>>
      tls_slot_;

  // Real socket interface and extension.
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptorExtension> extension_;

  // Mock thread local instance.
  NiceMock<ThreadLocal::MockInstance> thread_local_;

  // Mock dispatcher.
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  // Stats and config.
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
};

class UpstreamReverseConnectionAddressTest : public testing::Test {
public:
  UpstreamReverseConnectionAddressTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
  }

  void SetUp() override {}

  void TearDown() override {
    // Clean up thread local resources if they were set up.
    if (tls_slot_) {
      tls_slot_.reset();
    }
    // Don't reset thread_local_registry_ as it's owned by the extension.
    if (extension_) {
      extension_.reset();
    }
    if (socket_interface_) {
      socket_interface_.reset();
    }
  }

  // Set up the upstream extension components (socket interface and extension).
  void setupUpstreamExtension() {
    // Create the socket interface.
    socket_interface_ =
        std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptor>(server_context_);

    // Create the extension.
    extension_ = std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptorExtension>(
        *socket_interface_, server_context_, config_);
  }

  // Set up the thread local slot with the extension.
  void setupThreadLocalSlot() {
    // Check if extension is set up
    if (!extension_) {
      return;
    }

    // Let the extension create and manage its own TLS slot and timer.
    extension_->onServerInitialized();

    // Get the registered socket interface from the global registry and set up its extension.
    auto* registered_socket_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (registered_socket_interface) {
      auto* registered_acceptor = dynamic_cast<BootstrapReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_socket_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered socket interface.
        registered_acceptor->extension_ = extension_.get();
      }
    }
  }

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  // Real thread local slot and registry for reverse connection testing.
  std::unique_ptr<ThreadLocal::TypedSlot<BootstrapReverseConnection::UpstreamSocketThreadLocal>>
      tls_slot_;

  // Real socket interface and extension.
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptorExtension> extension_;

  // Configuration for the extension.
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  // Stats store and scope.
  Stats::TestUtil::TestStore stats_store_;
  Stats::ScopeSharedPtr stats_scope_;

  // Thread local mock.
  NiceMock<ThreadLocal::MockInstance> thread_local_;
};

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
