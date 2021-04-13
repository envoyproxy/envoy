#pragma once
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
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
#include "common/stream_info/stream_info_impl.h"

#include "server/active_stream_socket.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

struct ActiveInternalConnection;
using ActiveInternalConnectionPtr = std::unique_ptr<ActiveInternalConnection>;

class ActiveInternalConnections;
using ActiveInternalConnectionsPtr = std::unique_ptr<ActiveInternalConnections>;

class ActiveInternalListener : public ActiveStreamListenerBase,
                               public Network::InternalListenerCallbacks,
                               Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveInternalListener(Network::ConnectionHandler& conn_handler, Event::Dispatcher& dispatcher,
                         Network::ListenerConfig& config);
  ActiveInternalListener(Network::ConnectionHandler& conn_handler, Event::Dispatcher& dispatcher,
                         Network::ListenerPtr listener, Network::ListenerConfig& config);
  ~ActiveInternalListener() override;

  class NetworkInternalListener : public Network::Listener {

    void disable() override {
      // TODO(lambdai): think about how to elegantly disable internal listener. (Queue socket or
      // close socket immediately?)
    }

    void enable() override {}

    void setRejectFraction(UnitFloat) override {}
  };
  // ActiveListenerImplBase
  Network::Listener* listener() override { return listener_.get(); }
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance&) override;

  void pauseListening() override {
    if (listener_ != nullptr) {
      listener_->disable();
    }
  }
  void resumeListening() override {
    if (listener_ != nullptr) {
      listener_->enable();
    }
  }
  void shutdownListener() override { listener_.reset(); }

  // Network::InternalListenerCallbacks
  void onAccept(Network::ConnectionSocketPtr&& socket) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  void incNumConnections() override { config_->openConnections().inc(); }
  void decNumConnections() override { config_->openConnections().dec(); }
  /**
   * Remove and destroy an active connection.
   * @param connection supplies the connection to remove.
   */
  void removeConnection(ActiveInternalConnection& connection);

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  void newConnection(Network::ConnectionSocketPtr&& socket,
                     std::unique_ptr<StreamInfo::StreamInfo> stream_info) override;

  /**
   * Return the active connections container attached with the given filter chain.
   */
  ActiveInternalConnections& getOrCreateActiveConnections(const Network::FilterChain& filter_chain);

  /**
   * Schedule to remove and destroy the active connections which are not tracked by listener
   * config. Caution: The connection are not destroyed yet when function returns.
   */
  void
  deferredRemoveFilterChains(const std::list<const Network::FilterChain*>& draining_filter_chains);

  /**
   * Update the listener config. The follow up connections will see the new config. The existing
   * connections are not impacted.
   */
  void updateListenerConfig(Network::ListenerConfig& config);

  absl::node_hash_map<const Network::FilterChain*, ActiveInternalConnectionsPtr>
      connections_by_context_;
  bool is_deleting_{false};
};

/**
 * Wrapper for a group of active connections which are attached to the same filter chain context.
 */
class ActiveInternalConnections : public Event::DeferredDeletable {
public:
  ActiveInternalConnections(ActiveInternalListener& listener,
                            const Network::FilterChain& filter_chain);
  ~ActiveInternalConnections() override;

  // Listener filter chain pair is the owner of the connections.
  ActiveInternalListener& listener_;
  const Network::FilterChain& filter_chain_;
  // Owned connections
  std::list<ActiveInternalConnectionPtr> connections_;
};

/**
 * Wrapper for an active internal connection owned by this handler.
 */
struct ActiveInternalConnection : LinkedObject<ActiveInternalConnection>,
                                  public Event::DeferredDeletable,
                                  public Network::ConnectionCallbacks {
  ActiveInternalConnection(ActiveInternalConnections& active_connections,
                           Network::ConnectionPtr&& new_connection, TimeSource& time_system,
                           std::unique_ptr<StreamInfo::StreamInfo>&& stream_info);
  ~ActiveInternalConnection() override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    // Any event leads to destruction of the connection.
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      active_connections_.listener_.removeConnection(*this);
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  ActiveInternalConnections& active_connections_;
  Network::ConnectionPtr connection_;
  Stats::TimespanPtr conn_length_;
};

} // namespace Server
} // namespace Envoy
