#pragma once

#include "envoy/api/api.h"
#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
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
class ConnectionHandler final : NonCopyable {
public:
  ConnectionHandler(Stats::Store& stats_store, spdlog::logger& logger, Api::ApiPtr&& api);
  ~ConnectionHandler();

  Api::Api& api() { return *api_; }
  Event::Dispatcher& dispatcher() { return *dispatcher_; }
  uint64_t numConnections() { return num_connections_; }

  /**
   * Adds listener to the handler.
   * @param factory supplies the configuration factory for new connections.
   * @param socket supplies the already bound socket to listen on.
   * @param bind_to_port specifies if the listener should actually bind to the port.
   *        a listener that doesn't bind can only receive connections redirected from
   *        other listeners that set use_origin_dst to true
   * @param use_proxy_proto whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   * @param use_orig_dst if a connection was redirected to this port using iptables,
   *        allow the listener to hand it off to the listener associated to the original port
   */
  void addListener(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                   bool bind_to_port, bool use_proxy_proto, bool use_orig_dst);

  /**
   * Adds listener to the handler.
   * @param factory supplies the configuration factory for new connections.
   * @param socket supplies the already bound socket to listen on.
   * @param bind_to_port specifies if the listener should actually bind to the port.
   *        a listener that doesn't bind can only receive connections redirected from
   *        other listeners that set use_origin_dst to true
   * @param use_proxy_proto whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   * @param use_orig_dst if a connection was redirected to this port using iptables,
   *        allow the listener to hand it off to the listener associated to the original port
   */
  void addSslListener(Network::FilterChainFactory& factory, Ssl::ServerContext& ssl_ctx,
                      Network::ListenSocket& socket, bool bind_to_port, bool use_proxy_proto,
                      bool use_orig_dst);

  /**
   * Find a listener based on the provided socket name
   * @param name supplies the name of the socket
   * @return a pointer to the listener or nullptr if not found.
   * Ownership of the listener is NOT transferred
   */
  Network::Listener* findListener(const std::string& socket_name);

  /**
   * Close and destroy all connections. This must be called from the same thread that is running
   * the dispatch loop.
   */
  void closeConnections();

  /**
   * Close and destroy all listeners. Existing connections will not be effected. This must be
   * called from the same thread that is running the dispatch loop.
   */
  void closeListeners();

  /**
   * Start a watchdog that attempts to tick every 100ms and will increment a stat if a tick takes
   * more than 200ms in real time.
   */
  void startWatchdog();

private:
  /**
   * Wrapper for an active listener owned by this worker.
   */
  struct ActiveListener : public Network::ListenerCallbacks {
    ActiveListener(ConnectionHandler& parent, Network::ListenSocket& socket,
                   Network::FilterChainFactory& factory, bool use_proxy_proto, bool bind_to_port,
                   bool use_orig_dst);

    ActiveListener(ConnectionHandler& parent, Network::ListenerPtr&& listener,
                   Network::FilterChainFactory& factory, const std::string& stats_prefix);

    /**
     * Fires when a new connection is received from the listener.
     * @param new_connection supplies the connection to take control of.
     */
    void onNewConnection(Network::ConnectionPtr&& new_connection) override;

    ConnectionHandler& parent_;
    Network::FilterChainFactory& factory_;
    Network::ListenerPtr listener_;
    ListenerStats stats_;
  };

  struct SslActiveListener : public ActiveListener {
    SslActiveListener(ConnectionHandler& parent, Ssl::ServerContext& ssl_ctx,
                      Network::ListenSocket& socket, Network::FilterChainFactory& factory,
                      bool use_proxy_proto, bool bind_to_port, bool use_orig_dst);
  };

  typedef std::unique_ptr<ActiveListener> ActiveListenerPtr;

  /**
   * Wrapper for an active connection owned by this worker.
   */
  struct ActiveConnection : LinkedObject<ActiveConnection>,
                            public Event::DeferredDeletable,
                            public Network::ConnectionCallbacks {
    ActiveConnection(ConnectionHandler& parent, Network::ConnectionPtr&& new_connection,
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

    ConnectionHandler& parent_;
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
  std::map<std::string, ActiveListenerPtr> listeners_;
  std::list<ActiveConnectionPtr> connections_;
  std::atomic<uint64_t> num_connections_{};
  Stats::Counter& watchdog_miss_counter_;
  Stats::Counter& watchdog_mega_miss_counter_;
  Event::TimerPtr watchdog_timer_;
  SystemTime last_watchdog_time_;
};
