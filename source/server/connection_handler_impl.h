#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "common/common/linked_object.h"
#include "common/common/non_copyable.h"
#include "common/stream_info/stream_info_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

#define ALL_LISTENER_STATS(COUNTER, GAUGE, HISTOGRAM)                                              \
  COUNTER(downstream_cx_destroy)                                                                   \
  COUNTER(downstream_cx_overflow)                                                                  \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_overload_reject)                                                           \
  COUNTER(downstream_global_cx_overflow)                                                           \
  COUNTER(downstream_pre_cx_timeout)                                                               \
  COUNTER(no_filter_chain_match)                                                                   \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_pre_cx_active, Accumulate)                                                      \
  HISTOGRAM(downstream_cx_length_ms, Milliseconds)

/**
 * Wrapper struct for listener stats. @see stats_macros.h
 */
struct ListenerStats {
  ALL_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

#define ALL_PER_HANDLER_LISTENER_STATS(COUNTER, GAUGE)                                             \
  COUNTER(downstream_cx_total)                                                                     \
  GAUGE(downstream_cx_active, Accumulate)

/**
 * Wrapper struct for per-handler listener stats. @see stats_macros.h
 */
struct PerHandlerListenerStats {
  ALL_PER_HANDLER_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class ActiveUdpListenerBase;
class ActiveTcpListener;

/**
 * Server side connection handler. This is used both by workers as well as the
 * main thread for non-threaded listeners.
 */
class ConnectionHandlerImpl : public Network::TcpConnectionHandler,
                              public Network::UdpConnectionHandler,
                              NonCopyable,
                              Logger::Loggable<Logger::Id::conn_handler> {
public:
  using ActiveTcpListenerOptRef = absl::optional<std::reference_wrapper<ActiveTcpListener>>;
  using UdpListenerCallbacksOptRef =
      absl::optional<std::reference_wrapper<Network::UdpListenerCallbacks>>;
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

private:
  friend struct ActiveTcpSocket;
  struct ActiveTcpListenerDetail {
  public:
    virtual ~ActiveTcpListenerDetail() = default;
    virtual ActiveTcpListenerOptRef tcpListener() PURE;
  };
  struct ActiveUdpListenerDetail {
  public:
    virtual ~ActiveUdpListenerDetail() = default;
    virtual UdpListenerCallbacksOptRef udpListener() PURE;
  };
  struct ActiveListenerDetails : public ActiveTcpListenerDetail, public ActiveUdpListenerDetail {
    // Strong pointer to the listener, whether TCP, UDP, QUIC, etc.
    Network::ConnectionHandler::ActiveListenerPtr listener_;

    absl::variant<absl::monostate, std::reference_wrapper<ActiveTcpListener>,
                  std::reference_wrapper<Network::UdpListenerCallbacks>>
        typed_listener_;

    // Helpers for accessing the data in the variant for cleaner code.
    ActiveTcpListenerOptRef tcpListener() override;
    UdpListenerCallbacksOptRef udpListener() override;
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

/**
 * Wrapper for an active listener owned by this handler.
 */
class ActiveListenerImplBase : public virtual Network::ConnectionHandler::ActiveListener {
public:
  ActiveListenerImplBase(Network::ConnectionHandler& parent, Network::ListenerConfig* config);

  // Network::ConnectionHandler::ActiveListener.
  uint64_t listenerTag() override { return config_->listenerTag(); }

  ListenerStats stats_;
  PerHandlerListenerStats per_worker_stats_;
  Network::ListenerConfig* config_{};
};

} // namespace Server
} // namespace Envoy
