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
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/common/non_copyable.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

// clang-format off
#define ALL_LISTENER_STATS(COUNTER, GAUGE, HISTOGRAM)                                              \
  COUNTER  (downstream_cx_total)                                                                   \
  COUNTER  (downstream_cx_destroy)                                                                 \
  GAUGE    (downstream_cx_active)                                                                  \
  HISTOGRAM(downstream_cx_length_ms)
// clang-format on

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
  void addListener(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                   Stats::Scope& scope, uint64_t listener_tag,
                   const Network::ListenerOptions& listener_options) override;
  void addSslListener(Network::FilterChainFactory& factory, Ssl::ServerContext& ssl_ctx,
                      Network::ListenSocket& socket, Stats::Scope& scope, uint64_t listener_tag,
                      const Network::ListenerOptions& listener_options) override;
  Network::Listener* findListenerByAddress(const Network::Address::Instance& address) override;
  void removeListeners(uint64_t listener_tag) override;
  void stopListeners(uint64_t listener_tag) override;
  void stopListeners() override;

private:
  struct ActiveConnection;
  typedef std::unique_ptr<ActiveConnection> ActiveConnectionPtr;

  /**
   * Wrapper for an active listener owned by this handler.
   */
  struct ActiveListener : public Network::ListenerCallbacks {
    ActiveListener(ConnectionHandlerImpl& parent, Network::ListenSocket& socket,
                   Network::FilterChainFactory& factory, Stats::Scope& scope, uint64_t listener_tag,
                   const Network::ListenerOptions& listener_options);

    ActiveListener(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                   Network::FilterChainFactory& factory, Stats::Scope& scope,
                   uint64_t listener_tag);

    ~ActiveListener();

    /**
     * Fires when a new connection is received from the listener.
     * @param new_connection supplies the connection to take control of.
     */
    void onNewConnection(Network::ConnectionPtr&& new_connection) override;

    /**
     * Remove and destroy an active connection.
     * @param connection supplies the connection to remove.
     */
    void removeConnection(ActiveConnection& connection);

    ConnectionHandlerImpl& parent_;
    Network::FilterChainFactory& factory_;
    Network::ListenerPtr listener_;
    ListenerStats stats_;
    std::list<ActiveConnectionPtr> connections_;
    const uint64_t listener_tag_;
  };

  struct SslActiveListener : public ActiveListener {
    SslActiveListener(ConnectionHandlerImpl& parent, Ssl::ServerContext& ssl_ctx,
                      Network::ListenSocket& socket, Network::FilterChainFactory& factory,
                      Stats::Scope& scope, uint64_t listener_tag,
                      const Network::ListenerOptions& listener_options);
  };

  typedef std::unique_ptr<ActiveListener> ActiveListenerPtr;

  /**
   * Wrapper for an active connection owned by this handler.
   */
  struct ActiveConnection : LinkedObject<ActiveConnection>,
                            public Event::DeferredDeletable,
                            public Network::ConnectionCallbacks {
    ActiveConnection(ActiveListener& listener, Network::ConnectionPtr&& new_connection);
    ~ActiveConnection();

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

    ActiveListener& listener_;
    Network::ConnectionPtr connection_;
    Stats::TimespanPtr conn_length_;
  };

  static ListenerStats generateStats(Stats::Scope& scope);

#ifndef NVLOG
  spdlog::logger& logger_;
#endif
  Event::Dispatcher& dispatcher_;
  std::list<std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>> listeners_;
  std::atomic<uint64_t> num_connections_{};
};

} // Server
} // namespace Envoy
