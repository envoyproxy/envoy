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
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/common/non_copyable.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

#define ALL_LISTENER_STATS(COUNTER, GAUGE, HISTOGRAM)                                              \
  COUNTER(downstream_cx_destroy)                                                                   \
  COUNTER(downstream_cx_total)                                                                     \
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

/**
 * Server side connection handler. This is used both by workers as well as the
 * main thread for non-threaded listeners.
 */
class ConnectionHandlerImpl : public Network::ConnectionHandler,
                              NonCopyable,
                              Logger::Loggable<Logger::Id::conn_handler> {
public:
  ConnectionHandlerImpl(Event::Dispatcher& dispatcher, const std::string& per_handler_stat_prefix);

  // Network::ConnectionHandler
  uint64_t numConnections() override { return num_handler_connections_; }
  void incNumConnections() override;
  void decNumConnections() override;
  void addListener(Network::ListenerConfig& config) override;
  void removeListeners(uint64_t listener_tag) override;
  void stopListeners(uint64_t listener_tag) override;
  void stopListeners() override;
  void disableListeners() override;
  void enableListeners() override;
  const std::string& statPrefix() override { return per_handler_stat_prefix_; }

  /**
   * Wrapper for an active listener owned by this handler.
   */
  class ActiveListenerImplBase : public Network::ConnectionHandler::ActiveListener {
  public:
    ActiveListenerImplBase(Network::ConnectionHandler& parent, Network::ListenerConfig& config);

    // Network::ConnectionHandler::ActiveListener.
    uint64_t listenerTag() override { return config_.listenerTag(); }

    ListenerStats stats_;
    PerHandlerListenerStats per_worker_stats_;
    Network::ListenerConfig& config_;
  };

private:
  struct ActiveTcpConnection;
  using ActiveTcpConnectionPtr = std::unique_ptr<ActiveTcpConnection>;
  struct ActiveTcpSocket;
  using ActiveTcpSocketPtr = std::unique_ptr<ActiveTcpSocket>;

  /**
   * Wrapper for an active tcp listener owned by this handler.
   */
  class ActiveTcpListener : public Network::ListenerCallbacks,
                            public ActiveListenerImplBase,
                            public Network::BalancedConnectionHandler {
  public:
    ActiveTcpListener(ConnectionHandlerImpl& parent, Network::ListenerConfig& config);
    ActiveTcpListener(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                      Network::ListenerConfig& config);
    ~ActiveTcpListener() override;
    void onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                        bool hand_off_restored_destination_connections, bool rebalanced);
    void decNumConnections() {
      ASSERT(num_listener_connections_ > 0);
      --num_listener_connections_;
    }

    // Network::ListenerCallbacks
    void onAccept(Network::ConnectionSocketPtr&& socket) override;

    // ActiveListenerImplBase
    Network::Listener* listener() override { return listener_.get(); }
    void destroy() override { listener_.reset(); }

    // Network::BalancedConnectionHandler
    uint64_t numConnections() const override { return num_listener_connections_; }
    void incNumConnections() override { ++num_listener_connections_; }
    void post(Network::ConnectionSocketPtr&& socket) override;

    /**
     * Remove and destroy an active connection.
     * @param connection supplies the connection to remove.
     */
    void removeConnection(ActiveTcpConnection& connection);

    /**
     * Create a new connection from a socket accepted by the listener.
     */
    void newConnection(Network::ConnectionSocketPtr&& socket);

    ConnectionHandlerImpl& parent_;
    Network::ListenerPtr listener_;
    const std::chrono::milliseconds listener_filters_timeout_;
    const bool continue_on_listener_filters_timeout_;
    std::list<ActiveTcpSocketPtr> sockets_;
    std::list<ActiveTcpConnectionPtr> connections_;

    // The number of connections currently active on this listener. This is typically used for
    // connection balancing across per-handler listeners.
    std::atomic<uint64_t> num_listener_connections_{};
  };

  /**
   * Wrapper for an active TCP connection owned by this handler.
   */
  struct ActiveTcpConnection : LinkedObject<ActiveTcpConnection>,
                               public Event::DeferredDeletable,
                               public Network::ConnectionCallbacks {
    ActiveTcpConnection(ActiveTcpListener& listener, Network::ConnectionPtr&& new_connection,
                        TimeSource& time_system);
    ~ActiveTcpConnection() override;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      // Any event leads to destruction of the connection.
      if (event == Network::ConnectionEvent::LocalClose ||
          event == Network::ConnectionEvent::RemoteClose) {
        listener_.removeConnection(*this);
      }
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ActiveTcpListener& listener_;
    Network::ConnectionPtr connection_;
    Stats::TimespanPtr conn_length_;
  };

  /**
   * Wrapper for an active accepted TCP socket owned by this handler.
   */
  struct ActiveTcpSocket : public Network::ListenerFilterManager,
                           public Network::ListenerFilterCallbacks,
                           LinkedObject<ActiveTcpSocket>,
                           public Event::DeferredDeletable {
    ActiveTcpSocket(ActiveTcpListener& listener, Network::ConnectionSocketPtr&& socket,
                    bool hand_off_restored_destination_connections)
        : listener_(listener), socket_(std::move(socket)),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          iter_(accept_filters_.end()) {
      listener_.stats_.downstream_pre_cx_active_.inc();
    }
    ~ActiveTcpSocket() override {
      accept_filters_.clear();
      listener_.stats_.downstream_pre_cx_active_.dec();

      // If the underlying socket is no longer attached, it means that it has been transferred to
      // an active connection. In this case, the active connection will decrement the number
      // of listener connections.
      // TODO(mattklein123): In general the way we account for the number of listener connections
      // is incredibly fragile. Revisit this by potentially merging ActiveTcpSocket and
      // ActiveTcpConnection, having a shared object which does accounting (but would require
      // another allocation, etc.).
      if (socket_ != nullptr) {
        listener_.decNumConnections();
      }
    }

    void onTimeout();
    void startTimer();
    void unlink();
    void newConnection();

    // Network::ListenerFilterManager
    void addAcceptFilter(Network::ListenerFilterPtr&& filter) override {
      accept_filters_.emplace_back(std::move(filter));
    }

    // Network::ListenerFilterCallbacks
    Network::ConnectionSocket& socket() override { return *socket_.get(); }
    Event::Dispatcher& dispatcher() override { return listener_.parent_.dispatcher_; }
    void continueFilterChain(bool success) override;

    ActiveTcpListener& listener_;
    Network::ConnectionSocketPtr socket_;
    const bool hand_off_restored_destination_connections_;
    std::list<Network::ListenerFilterPtr> accept_filters_;
    std::list<Network::ListenerFilterPtr>::iterator iter_;
    Event::TimerPtr timer_;
  };

  using ActiveTcpListenerOptRef = absl::optional<std::reference_wrapper<ActiveTcpListener>>;

  struct ActiveListenerDetails {
    // Strong pointer to the listener, whether TCP, UDP, QUIC, etc.
    Network::ConnectionHandler::ActiveListenerPtr listener_;
    // Reference to the listener IFF this is a TCP listener. Null otherwise.
    ActiveTcpListenerOptRef tcp_listener_;
  };

  ActiveTcpListenerOptRef findActiveTcpListenerByAddress(const Network::Address::Instance& address);

  Event::Dispatcher& dispatcher_;
  const std::string per_handler_stat_prefix_;
  std::list<std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerDetails>> listeners_;
  std::atomic<uint64_t> num_handler_connections_{};
  bool disable_listeners_;
};

/**
 * Wrapper for an active udp listener owned by this handler.
 * TODO(danzh): rename to ActiveRawUdpListener.
 */
class ActiveUdpListener : public Network::UdpListenerCallbacks,
                          public ConnectionHandlerImpl::ActiveListenerImplBase,
                          public Network::UdpListenerFilterManager,
                          public Network::UdpReadFilterCallbacks {
public:
  ActiveUdpListener(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                    Network::ListenerConfig& config);
  ActiveUdpListener(Network::ConnectionHandler& parent, Network::UdpListenerPtr&& listener,
                    Network::ListenerConfig& config);

  // Network::UdpListenerCallbacks
  void onData(Network::UdpRecvData& data) override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(const Network::UdpListenerCallbacks::ErrorCode& error_code,
                      Api::IoError::IoErrorCode err) override;

  // ActiveListenerImplBase
  Network::Listener* listener() override { return udp_listener_.get(); }
  void destroy() override { udp_listener_.reset(); }

  // Network::UdpListenerFilterManager
  void addReadFilter(Network::UdpListenerReadFilterPtr&& filter) override;

  // Network::UdpReadFilterCallbacks
  Network::UdpListener& udpListener() override;

private:
  Network::UdpListenerPtr udp_listener_;
  Network::UdpListenerReadFilterPtr read_filter_;
};

} // namespace Server
} // namespace Envoy
