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

  ConnectionHandlerImpl(Event::Dispatcher& dispatcher, absl::optional<uint32_t> worker_index);

  // Network::ConnectionHandler
  uint64_t numConnections() const override { return num_handler_connections_; }
  void incNumConnections() override;
  void decNumConnections() override;
  void addListener(absl::optional<uint64_t> overridden_listener, Network::ListenerConfig& config,
                   Runtime::Loader& runtime,
                   OptRef<const std::vector<Network::Address::InstanceConstSharedPtr>>
                       new_addresses = absl::nullopt) override;
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
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByTag(uint64_t listener_tag,
                          const Network::Address::Instance& address) override;
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) override;

  // Network::UdpConnectionHandler
  Network::UdpListenerCallbacksOptRef
  getUdpListenerCallbacks(uint64_t listener_tag,
                          const Network::Address::Instance& address) override;

  // Network::InternalListenerManager
  Network::InternalListenerOptRef
  findByAddress(const Network::Address::InstanceConstSharedPtr& listen_address) override;

private:
  struct PerAddressActiveListenerDetails {
    // Strong pointer to the listener, whether TCP, UDP, QUIC, etc.
    Network::ConnectionHandler::ActiveListenerPtr listener_;
    Network::Address::InstanceConstSharedPtr address_;
    Network::Address::InstanceConstSharedPtr listen_address_;
    uint64_t listener_tag_;

    absl::variant<absl::monostate, std::reference_wrapper<ActiveTcpListener>,
                  std::reference_wrapper<Network::UdpListenerCallbacks>,
                  std::reference_wrapper<Network::InternalListener>>
        typed_listener_;

    // Helpers for accessing the data in the variant for cleaner code.
    ActiveTcpListenerOptRef tcpListener();
    UdpListenerCallbacksOptRef udpListener();
    Network::InternalListenerOptRef internalListener();
  };

  struct ActiveListenerDetails {
    std::vector<std::shared_ptr<PerAddressActiveListenerDetails>> per_address_details_list_;

    using ListenerMethodFn = std::function<void(Network::ConnectionHandler::ActiveListener&)>;

    /**
     * A helper to execute specific method on each PerAddressActiveListenerDetails item.
     */
    void invokeListenerMethod(ListenerMethodFn fn) {
      std::for_each(per_address_details_list_.begin(), per_address_details_list_.end(),
                    [&fn](std::shared_ptr<PerAddressActiveListenerDetails>& details) {
                      fn(*details->listener_);
                    });
    }

    /**
     * Add an ActiveListener into the list.
     */
    template <class ActiveListener>
    void addActiveListener(Network::ListenerConfig& config,
                           const Network::Address::InstanceConstSharedPtr& address,
                           const Network::Address::InstanceConstSharedPtr& listen_address,
                           UnitFloat& listener_reject_fraction, bool disable_listeners,
                           ActiveListener&& listener) {
      auto per_address_details = std::make_shared<PerAddressActiveListenerDetails>();
      per_address_details->typed_listener_ = *listener;
      per_address_details->listener_ = std::move(listener);
      per_address_details->address_ = address;
      per_address_details->listen_address_ = listen_address;
      if (disable_listeners) {
        per_address_details->listener_->pauseListening();
      }
      if (auto* listener = per_address_details->listener_->listener(); listener != nullptr) {
        listener->setRejectFraction(listener_reject_fraction);
      }
      per_address_details->listener_tag_ = config.listenerTag();
      per_address_details_list_.emplace_back(per_address_details);
    }
  };

  using ActiveListenerDetailsOptRef = absl::optional<std::reference_wrapper<ActiveListenerDetails>>;
  ActiveListenerDetailsOptRef findActiveListenerByTag(uint64_t listener_tag);

  using PerAddressActiveListenerDetailsOptRef =
      absl::optional<std::reference_wrapper<PerAddressActiveListenerDetails>>;
  PerAddressActiveListenerDetailsOptRef
  findPerAddressActiveListenerDetails(const ActiveListenerDetailsOptRef active_listener_details,
                                      const Network::Address::Instance& address);

  // This has a value on worker threads, and no value on the main thread.
  const absl::optional<uint32_t> worker_index_;
  Event::Dispatcher& dispatcher_;
  const std::string per_handler_stat_prefix_;
  // Declare before its users ActiveListenerDetails.
  std::atomic<uint64_t> num_handler_connections_{};
  absl::flat_hash_map<uint64_t, std::unique_ptr<ActiveListenerDetails>> listener_map_by_tag_;
  absl::flat_hash_map<std::string, std::shared_ptr<PerAddressActiveListenerDetails>>
      tcp_listener_map_by_address_;
  absl::flat_hash_map<std::string, std::shared_ptr<PerAddressActiveListenerDetails>>
      internal_listener_map_by_address_;

  bool disable_listeners_;
  UnitFloat listener_reject_fraction_{UnitFloat::min()};
};

} // namespace Server
} // namespace Envoy
