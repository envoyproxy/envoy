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
  HISTOGRAM(downstream_cx_length_ms)

/**
 * Wrapper struct for listener stats. @see stats_macros.h
 */
struct ListenerStats {
  ALL_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Server side connection handler. This is used both by workers as well as the
 * main thread for non-threaded listeners.
 */
class ConnectionHandlerImpl : public Network::ConnectionHandler, NonCopyable {
public:
  ConnectionHandlerImpl(spdlog::logger& logger, Event::Dispatcher& dispatcher);

  // Network::ConnectionHandler
  uint64_t numConnections() override { return num_connections_; }
  void addListener(Network::ListenerConfig& config) override;
  void removeListeners(uint64_t listener_tag) override;
  void stopListeners(uint64_t listener_tag) override;
  void stopListeners() override;
  void disableListeners() override;
  void enableListeners() override;

  Network::Listener* findListenerByAddress(const Network::Address::Instance& address) override;

private:
  struct ActiveListenerBase;
  using ActiveListenerBasePtr = std::unique_ptr<ActiveListenerBase>;

  struct ActiveTcpListener;
  using ActiveTcpListenerPtr = std::unique_ptr<ActiveTcpListener>;

  struct ActiveUdpListener;
  using ActiveUdpListenerPtr = std::unique_ptr<ActiveUdpListener>;

  ActiveListenerBase* findActiveListenerByAddress(const Network::Address::Instance& address);

  struct ActiveConnection;
  using ActiveConnectionPtr = std::unique_ptr<ActiveConnection>;
  struct ActiveSocket;
  using ActiveSocketPtr = std::unique_ptr<ActiveSocket>;

  /**
   * Wrapper for an active listener owned by this handler.
   */
  struct ActiveListenerBase {
    ActiveListenerBase(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                       Network::ListenerConfig& config);

    virtual ~ActiveListenerBase() = default;

    ConnectionHandlerImpl& parent_;
    Network::ListenerPtr listener_;
    ListenerStats stats_;
    const std::chrono::milliseconds listener_filters_timeout_;
    const uint64_t listener_tag_;
    Network::ListenerConfig& config_;
  };

  /**
   * Wrapper for an active udp listener owned by this handler.
   */
  struct ActiveUdpListener : public Network::UdpListenerCallbacks,
                             public ActiveListenerBase,
                             public Network::UdpListenerFilterManager,
                             public Network::UdpReadFilterCallbacks {
    ActiveUdpListener(ConnectionHandlerImpl& parent, Network::ListenerConfig& config);

    ActiveUdpListener(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                      Network::ListenerConfig& config);

    // Network::UdpListenerCallbacks
    void onData(Network::UdpRecvData& data) override;
    void onWriteReady(const Network::Socket& socket) override;
    void onReceiveError(const Network::UdpListenerCallbacks::ErrorCode& error_code,
                        Api::IoError::IoErrorCode err) override;

    // Network::UdpListenerFilterManager
    void addReadFilter(Network::UdpListenerReadFilterPtr&& filter) override;

    // Network::UdpReadFilterCallbacks
    Network::UdpListener& udpListener() override;

    Network::UdpListener* udp_listener_;
    Network::UdpListenerReadFilterPtr read_filter_;
  };

  /**
   * Wrapper for an active tcp listener owned by this handler.
   */
  struct ActiveTcpListener : public Network::ListenerCallbacks, public ActiveListenerBase {
    ActiveTcpListener(ConnectionHandlerImpl& parent, Network::ListenerConfig& config);

    ActiveTcpListener(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                      Network::ListenerConfig& config);

    ~ActiveTcpListener() override;

    // Network::ListenerCallbacks
    void onAccept(Network::ConnectionSocketPtr&& socket,
                  bool hand_off_restored_destination_connections) override;
    void onNewConnection(Network::ConnectionPtr&& new_connection) override;

    /**
     * Remove and destroy an active connection.
     * @param connection supplies the connection to remove.
     */
    void removeConnection(ActiveConnection& connection);

    /**
     * Create a new connection from a socket accepted by the listener.
     */
    void newConnection(Network::ConnectionSocketPtr&& socket);

    std::list<ActiveSocketPtr> sockets_;
    std::list<ActiveConnectionPtr> connections_;
  };

  /**
   * Wrapper for an active connection owned by this handler.
   */
  struct ActiveConnection : LinkedObject<ActiveConnection>,
                            public Event::DeferredDeletable,
                            public Network::ConnectionCallbacks {
    ActiveConnection(ActiveTcpListener& listener, Network::ConnectionPtr&& new_connection,
                     TimeSource& time_system);
    ~ActiveConnection() override;

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
   * Wrapper for an active accepted socket owned by this handler.
   */
  struct ActiveSocket : public Network::ListenerFilterManager,
                        public Network::ListenerFilterCallbacks,
                        LinkedObject<ActiveSocket>,
                        public Event::DeferredDeletable {
    ActiveSocket(ActiveTcpListener& listener, Network::ConnectionSocketPtr&& socket,
                 bool hand_off_restored_destination_connections)
        : listener_(listener), socket_(std::move(socket)),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          iter_(accept_filters_.end()) {
      listener_.stats_.downstream_pre_cx_active_.inc();
    }
    ~ActiveSocket() override {
      accept_filters_.clear();
      listener_.stats_.downstream_pre_cx_active_.dec();
    }

    void onTimeout();
    void startTimer();
    void unlink();

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

  static ListenerStats generateStats(Stats::Scope& scope);

  spdlog::logger& logger_;
  Event::Dispatcher& dispatcher_;
  std::list<std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerBasePtr>> listeners_;
  std::atomic<uint64_t> num_connections_{};
  bool disable_listeners_;
};

} // namespace Server
} // namespace Envoy
