#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/random_generator.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseTunnelAcceptor;
class ReverseTunnelAcceptorExtension;
class UpstreamSocketManager;

class UpstreamReverseConnectionIOHandle : public Network::IoSocketHandleImpl {
public:
  UpstreamReverseConnectionIOHandle(Network::ConnectionSocketPtr socket,
                                    const std::string& cluster_name);

  ~UpstreamReverseConnectionIOHandle() override;

  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;

  Api::IoCallUint64Result close() override;

  const Network::ConnectionSocket& getSocket() const { return *owned_socket_; }

private:
  std::string cluster_name_;
  Network::ConnectionSocketPtr owned_socket_;
};

class UpstreamSocketThreadLocal : public ThreadLocal::ThreadLocalObject {
public:
  UpstreamSocketThreadLocal(Event::Dispatcher& dispatcher,
                            ReverseTunnelAcceptorExtension* extension = nullptr)
      : dispatcher_(dispatcher),
        socket_manager_(std::make_unique<UpstreamSocketManager>(dispatcher, extension)) {}

  Event::Dispatcher& dispatcher() { return dispatcher_; }

  UpstreamSocketManager* socketManager() { return socket_manager_.get(); }
  const UpstreamSocketManager* socketManager() const { return socket_manager_.get(); }

private:
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<UpstreamSocketManager> socket_manager_;
};

class ReverseTunnelAcceptor : public Envoy::Network::SocketInterfaceBase,
                              public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
public:
  ReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context);

  ReverseTunnelAcceptor() : extension_(nullptr), context_(nullptr) {}

  Envoy::Network::IoHandlePtr socket(Envoy::Network::Socket::Type, Envoy::Network::Address::Type,
                                     Envoy::Network::Address::IpVersion, bool,
                                     const Envoy::Network::SocketCreationOptions&) const override;

  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr,
         const Envoy::Network::SocketCreationOptions& options) const override;

  bool ipFamilySupported(int domain) override;

  UpstreamSocketThreadLocal* getLocalRegistry() const;

  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override {
    return "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface";
  }

  ReverseTunnelAcceptorExtension* getExtension() const { return extension_; }

  ReverseTunnelAcceptorExtension* extension_{nullptr};

private:
  Server::Configuration::ServerFactoryContext* context_;
};

class ReverseTunnelAcceptorExtension
    : public Envoy::Network::SocketInterfaceExtension,
      public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
  friend class ReverseTunnelAcceptorExtensionTest;

public:
  ReverseTunnelAcceptorExtension(
      Envoy::Network::SocketInterface& sock_interface,
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
          UpstreamReverseConnectionSocketInterface& config)
      : Envoy::Network::SocketInterfaceExtension(sock_interface), context_(context),
        socket_interface_(static_cast<ReverseTunnelAcceptor*>(&sock_interface)) {
    ENVOY_LOG(debug,
              "ReverseTunnelAcceptorExtension: creating upstream reverse connection "
              "socket interface with stat_prefix: {}",
              stat_prefix_);
    stat_prefix_ =
        PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "upstream_reverse_connection");
    if (socket_interface_ != nullptr) {
      socket_interface_->extension_ = this;
    }
  }

  void onServerInitialized() override;

  void onWorkerThreadInitialized() override {}

  UpstreamSocketThreadLocal* getLocalRegistry() const;

  const std::string& statPrefix() const { return stat_prefix_; }

  std::pair<std::vector<std::string>, std::vector<std::string>>
  getConnectionStatsSync(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(5000));

  absl::flat_hash_map<std::string, uint64_t> getCrossWorkerStatMap();

  void updateConnectionStats(const std::string& node_id, const std::string& cluster_id,
                             bool increment);

  void updatePerWorkerConnectionStats(const std::string& node_id, const std::string& cluster_id,
                                      bool increment);

  absl::flat_hash_map<std::string, uint64_t> getPerWorkerStatMap();

  Stats::Scope& getStatsScope() const { return context_.scope(); }

  void
  setTestOnlyTLSRegistry(std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> slot) {
    tls_slot_ = std::move(slot);
  }

private:
  Server::Configuration::ServerFactoryContext& context_;
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  ReverseTunnelAcceptor* socket_interface_;
  std::string stat_prefix_;
};

class UpstreamSocketManager : public ThreadLocal::ThreadLocalObject,
                              public Logger::Loggable<Logger::Id::filter> {
  friend class TestUpstreamSocketManager;

public:
  UpstreamSocketManager(Event::Dispatcher& dispatcher,
                        ReverseTunnelAcceptorExtension* extension = nullptr);

  ~UpstreamSocketManager();

  void addConnectionSocket(const std::string& node_id, const std::string& cluster_id,
                           Network::ConnectionSocketPtr socket,
                           const std::chrono::seconds& ping_interval, bool rebalanced);

  Network::ConnectionSocketPtr getConnectionSocket(const std::string& node_id);

  void markSocketDead(const int fd);

  void pingConnections();

  void pingConnections(const std::string& node_id);

  void tryEnablePingTimer(const std::chrono::seconds& ping_interval);

  void cleanStaleNodeEntry(const std::string& node_id);

  void onPingResponse(Network::IoHandle& io_handle);

  ReverseTunnelAcceptorExtension* getUpstreamExtension() const { return extension_; }

  std::string getNodeID(const std::string& key);

private:
  Event::Dispatcher& dispatcher_;
  Random::RandomGeneratorPtr random_generator_;

  absl::flat_hash_map<std::string, std::list<Network::ConnectionSocketPtr>>
      accepted_reverse_connections_;
  absl::flat_hash_map<int, std::string> fd_to_node_map_;
  absl::flat_hash_map<std::string, std::string> node_to_cluster_map_;
  absl::flat_hash_map<std::string, std::vector<std::string>> cluster_to_node_map_;
  absl::flat_hash_map<int, Event::FileEventPtr> fd_to_event_map_;
  absl::flat_hash_map<int, Event::TimerPtr> fd_to_timer_map_;
  Event::TimerPtr ping_timer_;
  std::chrono::seconds ping_interval_{0};
  ReverseTunnelAcceptorExtension* extension_;
};

DECLARE_FACTORY(ReverseTunnelAcceptor);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
