#pragma once

#include "envoy/api/api.h"
#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/network/listen_socket.h"

#include "common/common/linked_object.h"
#include "common/common/non_copyable.h"
#include "common/network/listen_socket_impl.h"

// clang-format off
#define ALL_LISTENER_STATS(COUNTER, GAUGE, TIMER)                                                  \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_destroy)                                                                   \
  GAUGE  (downstream_cx_active)                                                                    \
  TIMER  (downstream_cx_length_ms)
// clang-format on

namespace Server {

/**
 * Wrapper struct for listener stats. @see stats_macros.h
 */
struct ListenerStats {
  ALL_LISTENER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
};

/**
 * Server side connection handler. This is used both by workers as well as the
 * main thread for non-threaded listeners.
 */
class ConnectionHandlerImpl : public Network::ConnectionHandler, NonCopyable {
public:
  ConnectionHandlerImpl(Stats::Store& stats_store, spdlog::logger& logger, Api::ApiPtr&& api);
  ~ConnectionHandlerImpl();

  Api::Api& api() { return *api_; }
  Event::Dispatcher& dispatcher() { return *dispatcher_; }

  /**
   * Close and destroy all connections.
   */
  void closeConnections();

  /**
   * Start a watchdog that attempts to tick every 100ms and will increment a stat if a tick takes
   * more than 200ms in real time.
   */
  void startWatchdog();

  // Network::ConnectionHandler
  uint64_t numConnections() override { return num_connections_; }

  void addListener(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                   bool bind_to_port, bool use_proxy_proto, bool use_orig_dst,
                   size_t per_connection_buffer_limit_bytes) override;

  void addSslListener(Network::FilterChainFactory& factory, Ssl::ServerContext& ssl_ctx,
                      Network::ListenSocket& socket, bool bind_to_port, bool use_proxy_proto,
                      bool use_orig_dst, size_t per_connection_buffer_limit_bytes) override;

  Network::Listener* findListenerByPort(uint32_t port) override;

  void closeListeners() override;

private:
  /**
   * Wrapper for an active listener owned by this worker.
   */
  struct ActiveListener : public Network::ListenerCallbacks {
    ActiveListener(ConnectionHandlerImpl& parent, Network::ListenSocket& socket,
                   Network::FilterChainFactory& factory, bool use_proxy_proto, bool bind_to_port,
                   bool use_orig_dst, size_t per_connection_buffer_limit_bytes);

    ActiveListener(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                   Network::FilterChainFactory& factory, const std::string& stats_prefix);

    /**
     * Fires when a new connection is received from the listener.
     * @param new_connection supplies the connection to take control of.
     */
    void onNewConnection(Network::ConnectionPtr&& new_connection) override;

    ConnectionHandlerImpl& parent_;
    Network::FilterChainFactory& factory_;
    Network::ListenerPtr listener_;
    ListenerStats stats_;
  };

  struct SslActiveListener : public ActiveListener {
    SslActiveListener(ConnectionHandlerImpl& parent, Ssl::ServerContext& ssl_ctx,
                      Network::ListenSocket& socket, Network::FilterChainFactory& factory,
                      bool use_proxy_proto, bool bind_to_port, bool use_orig_dst,
                      size_t per_connection_buffer_limit_bytes);
  };

  typedef std::unique_ptr<ActiveListener> ActiveListenerPtr;

  /**
   * Wrapper for an active connection owned by this worker.
   */
  struct ActiveConnection : LinkedObject<ActiveConnection>,
                            public Event::DeferredDeletable,
                            public Network::ConnectionCallbacks {
    ActiveConnection(ConnectionHandlerImpl& parent, Network::ConnectionPtr&& new_connection,
                     ListenerStats& stats);
    ~ActiveConnection();

    // Network::ConnectionCallbacks
    void onEvent(uint32_t event) override {
      // Any event leads to destruction of the connection.
      if (event == Network::ConnectionEvent::LocalClose ||
          event == Network::ConnectionEvent::RemoteClose) {
        parent_.removeConnection(*this);
      }
    }

    ConnectionHandlerImpl& parent_;
    Network::ConnectionPtr connection_;
    ListenerStats& stats_;
    Stats::TimespanPtr conn_length_;
  };

  typedef std::unique_ptr<ActiveConnection> ActiveConnectionPtr;

  /**
   * Remove and destroy an active connection.
   * @param connection supplies the connection to remove.
   */
  void removeConnection(ActiveConnection& connection);

  static ListenerStats generateStats(const std::string& prefix, Stats::Store& store);

  Stats::Store& stats_store_;
  spdlog::logger& logger_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::list<std::pair<Network::Address::InstancePtr, ActiveListenerPtr>> listeners_;
  std::list<ActiveConnectionPtr> connections_;
  std::atomic<uint64_t> num_connections_{};
  Stats::Counter& watchdog_miss_counter_;
  Stats::Counter& watchdog_mega_miss_counter_;
  Event::TimerPtr watchdog_timer_;
  SystemTime last_watchdog_time_;
};

typedef std::unique_ptr<ConnectionHandlerImpl> ConnectionHandlerImplPtr;

} // Server
