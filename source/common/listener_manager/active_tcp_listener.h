#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/linked_object.h"
#include "source/common/listener_manager/active_stream_listener_base.h"
#include "source/common/listener_manager/active_tcp_socket.h"
#include "source/server/active_listener_base.h"

namespace Envoy {
namespace Server {
namespace {
// Structure used to allow a unique_ptr to be captured in a posted lambda. See below.
struct RebalancedSocket {
  Network::ConnectionSocketPtr socket;
};
using RebalancedSocketSharedPtr = std::shared_ptr<RebalancedSocket>;
} // namespace

/**
 * Wrapper for an active tcp listener owned by this handler.
 */
class ActiveTcpListener final : public Network::TcpListenerCallbacks,
                                public OwnedActiveStreamListenerBase,
                                public Network::BalancedConnectionHandler {
public:
  ActiveTcpListener(Network::TcpConnectionHandler& parent, Network::ListenerConfig& config,
                    Runtime::Loader& runtime, Random::RandomGenerator& random,
                    Network::SocketSharedPtr&& socket,
                    Network::Address::InstanceConstSharedPtr& listen_address,
                    Network::ConnectionBalancer& connection_balancer,
                    ThreadLocalOverloadStateOptRef overload_state);
  ActiveTcpListener(Network::TcpConnectionHandler& parent, Network::ListenerPtr&& listener,
                    Network::Address::InstanceConstSharedPtr& listen_address,
                    Network::ListenerConfig& config,
                    Network::ConnectionBalancer& connection_balancer, Runtime::Loader& runtime);
  ~ActiveTcpListener() override;

  bool listenerConnectionLimitReached() const {
    // TODO(tonya11en): Delegate enforcement of per-listener connection limits to overload
    // manager.
    return !config_->openConnections().canCreate();
  }

  void decNumConnections() override {
    ASSERT(num_listener_connections_ > 0);
    --num_listener_connections_;
    config_->openConnections().dec();
  }

  // Network::TcpListenerCallbacks
  void onAccept(Network::ConnectionSocketPtr&& socket) override;
  void onReject(RejectCause) override;
  void recordConnectionsAcceptedOnSocketEvent(uint32_t connections_accepted) override;

  // ActiveListenerImplBase
  Network::Listener* listener() override { return listener_.get(); }
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) override;

  void pauseListening() override;
  void resumeListening() override;
  void shutdownListener(const Network::ExtraShutdownListenerOptions&) override {
    listener_.reset();
  }

  // Network::BalancedConnectionHandler
  uint64_t numConnections() const override { return num_listener_connections_; }
  void incNumConnections() override {
    ++num_listener_connections_;
    config_->openConnections().inc();
  }
  void post(Network::ConnectionSocketPtr&& socket) override;
  void onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                      bool hand_off_restored_destination_connections, bool rebalanced) override;

  void newActiveConnection(const Network::FilterChain& filter_chain,
                           Network::ServerConnectionPtr server_conn_ptr,
                           std::unique_ptr<StreamInfo::StreamInfo> stream_info) override;

  /**
   * Update the listener config. The follow up connections will see the new config. The existing
   * connections are not impacted.
   */
  void updateListenerConfig(Network::ListenerConfig& config) override;

  Network::TcpConnectionHandler& tcp_conn_handler_;
  // The number of connections currently active on this listener. This is typically used for
  // connection balancing across per-handler listeners.
  std::atomic<uint64_t> num_listener_connections_{};

  Network::ConnectionBalancer& connection_balancer_;
  // This is the address this listener is listening on. It's used to get the correct listener
  // when rebalancing. The accepted socket can't be used to get the listening address, since
  // the accepted socket's remote address can be another address than the listening address.
  Network::Address::InstanceConstSharedPtr listen_address_;
};

using ActiveTcpListenerOptRef = absl::optional<std::reference_wrapper<ActiveTcpListener>>;
} // namespace Server
} // namespace Envoy
