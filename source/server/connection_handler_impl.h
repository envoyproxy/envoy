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
  void addListener(Network::ListenerConfig& config) override;
  void removeListeners(uint64_t listener_tag) override;
  void stopListeners(uint64_t listener_tag) override;
  void stopListeners() override;

  Network::Listener* findListenerByAddress(const Network::Address::Instance& address) override;

private:
  struct ActiveListener;
  ActiveListener* findActiveListenerByAddress(const Network::Address::Instance& address);

  struct ActiveConnection;
  typedef std::unique_ptr<ActiveConnection> ActiveConnectionPtr;
  struct ActiveSocket;
  typedef std::unique_ptr<ActiveSocket> ActiveSocketPtr;

  /**
   * Wrapper for an active listener owned by this handler.
   */
  struct ActiveListener : public Network::ListenerCallbacks {
    ActiveListener(ConnectionHandlerImpl& parent, Network::ListenerConfig& config);

    ActiveListener(ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                   Network::ListenerConfig& config);

    ~ActiveListener();

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

    ConnectionHandlerImpl& parent_;
    Network::ListenerPtr listener_;
    ListenerStats stats_;
    std::list<ActiveSocketPtr> sockets_;
    std::list<ActiveConnectionPtr> connections_;
    const uint64_t listener_tag_;
    Network::ListenerConfig& config_;
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

  /**
   * Wrapper for an active accepted socket owned by this handler.
   */
  struct ActiveSocket : public Network::ListenerFilterManager,
                        public Network::ListenerFilterCallbacks,
                        LinkedObject<ActiveSocket>,
                        public Event::DeferredDeletable {
    ActiveSocket(ActiveListener& listener, Network::ConnectionSocketPtr&& socket,
                 bool hand_off_restored_destination_connections)
        : listener_(listener), socket_(std::move(socket)),
          hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
          iter_(accept_filters_.end()) {}
    ~ActiveSocket() { accept_filters_.clear(); }

    // Network::ListenerFilterManager
    void addAcceptFilter(Network::ListenerFilterPtr&& filter) override {
      accept_filters_.emplace_back(std::move(filter));
    }

    // Network::ListenerFilterCallbacks
    Network::ConnectionSocket& socket() override { return *socket_.get(); }
    Event::Dispatcher& dispatcher() override { return listener_.parent_.dispatcher_; }
    void continueFilterChain(bool success) override;

    ActiveListener& listener_;
    Network::ConnectionSocketPtr socket_;
    const bool hand_off_restored_destination_connections_;
    std::list<Network::ListenerFilterPtr> accept_filters_;
    std::list<Network::ListenerFilterPtr>::iterator iter_;
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
