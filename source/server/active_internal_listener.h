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

#include "source/common/common/linked_object.h"
#include "source/common/common/non_copyable.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/server/active_stream_socket.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

struct ActiveInternalConnection;
using ActiveInternalConnectionPtr = std::unique_ptr<ActiveInternalConnection>;

class ActiveInternalConnections;
using ActiveInternalConnectionsPtr = std::unique_ptr<ActiveInternalConnections>;

class ActiveInternalListener;
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
  // Owned connections.
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

  using CollectionType = ActiveInternalConnections;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  ActiveInternalConnections& active_connections_;
  Network::ConnectionPtr connection_;
  Stats::TimespanPtr conn_length_;
};

class ActiveInternalListener : public TypedActiveStreamListenerBase<ActiveInternalConnection>,
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
      ENVOY_LOG(debug, "Warning: the internal listener cannot be disabled.");
    }

    void enable() override {
      ENVOY_LOG(debug, "Warning: the internal listener is always enabled.");
    }

    void setRejectFraction(UnitFloat) override {}
  };
  // ActiveListenerImplBase
  Network::Listener* listener() override { return listener_.get(); }
  Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

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
   * Update the listener config. The follow up connections will see the new config. The existing
   * connections are not impacted.
   */
  void updateListenerConfig(Network::ListenerConfig& config);
};

} // namespace Server
} // namespace Envoy
