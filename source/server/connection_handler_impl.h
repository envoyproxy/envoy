#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/common/non_copyable.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

class ActiveUdpListenerBase;
class ActiveTcpListener;
class ActiveInternalListener;

/**
 * Server side connection handler. This is used both by workers as well as the
 * main thread for non-threaded listeners.
 */
class ConnectionHandlerImpl : public Network::TcpConnectionHandler,
                              public Network::UdpConnectionHandler,
                              public Network::InternalListenerManager,
                              NonCopyable,
                              Logger::Loggable<Logger::Id::conn_handler> {
public:
  using UdpListenerCallbacksOptRef =
      absl::optional<std::reference_wrapper<Network::UdpListenerCallbacks>>;
  using ActiveTcpListenerOptRef = absl::optional<std::reference_wrapper<ActiveTcpListener>>;
  using ActiveInternalListenerOptRef =
      absl::optional<std::reference_wrapper<ActiveInternalListener>>;

  ConnectionHandlerImpl(Event::Dispatcher& dispatcher, absl::optional<uint32_t> worker_index);

  // Network::ConnectionHandler
  uint64_t numConnections() const override { return num_handler_connections_; }
  void incNumConnections() override;
  void decNumConnections() override;
  void addListener(absl::optional<uint64_t> overridden_listener,
                   Network::ListenerConfig& config) override;
  void removeListeners(uint64_t listener_tag) override;
  void removeFilterChains(uint64_t listener_tag,
                          const std::list<const Network::FilterChain*>& filter_chains,
                          std::function<void()> completion) override;
  void stopListeners(uint64_t listener_tag) override;
  void stopListeners() override;
  void disableListeners() override;
  void enableListeners() override;
  void setListenerRejectFraction(UnitFloat reject_fraction) override;
  const std::string& statPrefix() const override { return per_handler_stat_prefix_; }

  // Network::TcpConnectionHandler
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  Network::BalancedConnectionHandlerOptRef getBalancedHandlerByTag(uint64_t listener_tag) override;
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) override;

  // Network::UdpConnectionHandler
  Network::UdpListenerCallbacksOptRef getUdpListenerCallbacks(uint64_t listener_tag) override;

  // Network::InternalListenerManager
  Network::InternalListenerOptRef
  findByAddress(const Network::Address::InstanceConstSharedPtr& listen_address) override;

private:
  struct ActiveListenerDetails {
    // Strong pointer to the listener, whether TCP, UDP, QUIC, etc.
    Network::ConnectionHandler::ActiveListenerPtr listener_;

    absl::variant<absl::monostate, std::reference_wrapper<ActiveTcpListener>,
                  std::reference_wrapper<Network::UdpListenerCallbacks>,
                  std::reference_wrapper<ActiveInternalListener>>
        typed_listener_;

    // Helpers for accessing the data in the variant for cleaner code.
    ActiveTcpListenerOptRef tcpListener();
    UdpListenerCallbacksOptRef udpListener();
    ActiveInternalListenerOptRef internalListener();
  };
  using ActiveListenerDetailsOptRef = absl::optional<std::reference_wrapper<ActiveListenerDetails>>;
  ActiveListenerDetailsOptRef findActiveListenerByTag(uint64_t listener_tag);

  // This has a value on worker threads, and no value on the main thread.
  const absl::optional<uint32_t> worker_index_;
  Event::Dispatcher& dispatcher_;
  const std::string per_handler_stat_prefix_;
  std::list<std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerDetails>> listeners_;
  std::atomic<uint64_t> num_handler_connections_{};
  bool disable_listeners_;
  UnitFloat listener_reject_fraction_{UnitFloat::min()};
};

} // namespace Server
} // namespace Envoy
