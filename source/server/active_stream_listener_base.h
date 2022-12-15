#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/stats/timespan.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/linked_object.h"
#include "source/server/active_listener_base.h"
#include "source/server/active_tcp_socket.h"

namespace Envoy {
namespace Server {

// The base class of the stream listener. It owns listener filter handling of active sockets.
// After the active socket passes all the listener filters, a server connection is created. The
// derived listener must override ``newActiveConnection`` to take the ownership of that server
// connection.
// TODO(lambdai): Refactor the listener filter test cases to adopt this class.
class ActiveStreamListenerBase : public ActiveListenerImplBase,
                                 protected Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                           Network::ListenerPtr&& listener, Network::ListenerConfig& config);
  static void emitLogs(Network::ListenerConfig& config, StreamInfo::StreamInfo& stream_info);

  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * Schedule to remove and destroy the active connections which are not tracked by listener
   * config. Caution: The connection are not destroyed yet when function returns.
   */
  void onFilterChainDraining(
      const std::list<const Network::FilterChain*>& draining_filter_chains) override {
    // Need to recover the original deleting state.
    const bool was_deleting = is_deleting_;
    is_deleting_ = true;
    for (const auto* filter_chain : draining_filter_chains) {
      removeFilterChain(filter_chain);
    }
    is_deleting_ = was_deleting;
  }

  virtual void incNumConnections() PURE;
  virtual void decNumConnections() PURE;

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  void newConnection(Network::ConnectionSocketPtr&& socket,
                     std::unique_ptr<StreamInfo::StreamInfo> stream_info);

  /**
   * Remove the socket from this listener. Should be called when the socket passes the listener
   * filter.
   * @return std::unique_ptr<ActiveTcpSocket> the exact same socket in the parameter but in the
   * state that not owned by the listener.
   */
  std::unique_ptr<ActiveTcpSocket> removeSocket(ActiveTcpSocket&& socket) {
    return socket.removeFromList(sockets_);
  }

  /**
   * @return const std::list<std::unique_ptr<ActiveTcpSocket>>& the sockets going through the
   * listener filters.
   */
  const std::list<std::unique_ptr<ActiveTcpSocket>>& sockets() const { return sockets_; }

  /**
   * Schedule removal and destruction of all active connections owned by a filter chain.
   */
  virtual void removeFilterChain(const Network::FilterChain* filter_chain) PURE;

  virtual Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) PURE;

  void onSocketAccepted(std::unique_ptr<ActiveTcpSocket> active_socket) {
    // Create and run the filters
    if (config_->filterChainFactory().createListenerFilterChain(*active_socket)) {
      active_socket->startFilterChain();
    } else {
      // If create listener filter chain failed, it means the listener is missing
      // config due to the ECDS. Then close the connection directly.
      active_socket->socket().close();
      ASSERT(active_socket->isEndFilterIteration());
    }

    // Move active_socket to the sockets_ list if filter iteration needs to continue later.
    // Otherwise we let active_socket be destructed when it goes out of scope.
    if (!active_socket->isEndFilterIteration()) {
      active_socket->startTimer();
      LinkedList::moveIntoListBack(std::move(active_socket), sockets_);
    } else {
      if (!active_socket->connected()) {
        // If active_socket is about to be destructed, emit logs if a connection is not created.
        if (active_socket->streamInfo() != nullptr) {
          emitLogs(*config_, *active_socket->streamInfo());
        } else {
          // If the active_socket is not connected, this socket is not promoted to active
          // connection. Thus the stream_info_ is owned by this active socket.
          ENVOY_BUG(active_socket->streamInfo() != nullptr,
                    "the unconnected active socket must have stream info.");
        }
      }
    }
  }

  // Below members are open to access by ActiveTcpSocket.
  Network::ConnectionHandler& parent_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;

protected:
  /**
   * Create the active connection from server connection. This active listener owns the created
   * active connection.
   *
   * @param filter_chain The network filter chain linking to the connection.
   * @param server_conn_ptr The server connection.
   * @param stream_info The stream info of the active connection.
   */
  virtual void newActiveConnection(const Network::FilterChain& filter_chain,
                                   Network::ServerConnectionPtr server_conn_ptr,
                                   std::unique_ptr<StreamInfo::StreamInfo> stream_info) PURE;

  std::list<std::unique_ptr<ActiveTcpSocket>> sockets_;
  Network::ListenerPtr listener_;
  // True if the follow up connection deletion is raised by the connection collection deletion is
  // performing. Otherwise, the collection should be deleted when the last connection in the
  // collection is removed. This state is maintained in base class because this state is independent
  // from concrete connection type.
  bool is_deleting_{false};

private:
  Event::Dispatcher& dispatcher_;
};

struct ActiveTcpConnection;
class OwnedActiveStreamListenerBase;

/**
 * Wrapper for a group of active connections which are attached to the same filter chain context.
 */
class ActiveConnections : public Event::DeferredDeletable {
public:
  ActiveConnections(OwnedActiveStreamListenerBase& listener,
                    const Network::FilterChain& filter_chain);
  ~ActiveConnections() override;

  // listener filter chain pair is the owner of the connections
  OwnedActiveStreamListenerBase& listener_;
  const Network::FilterChain& filter_chain_;
  // Owned connections.
  std::list<std::unique_ptr<ActiveTcpConnection>> connections_;
};

/**
 * Wrapper for an active TCP connection owned by this handler.
 */
struct ActiveTcpConnection : LinkedObject<ActiveTcpConnection>,
                             public Event::DeferredDeletable,
                             public Network::ConnectionCallbacks,
                             Logger::Loggable<Logger::Id::conn_handler> {
  ActiveTcpConnection(ActiveConnections& active_connections,
                      Network::ConnectionPtr&& new_connection, TimeSource& time_system,
                      std::unique_ptr<StreamInfo::StreamInfo>&& stream_info);
  ~ActiveTcpConnection() override;
  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  ActiveConnections& active_connections_;
  Network::ConnectionPtr connection_;
  Stats::TimespanPtr conn_length_;
};

using ActiveConnectionPtr = std::unique_ptr<ActiveTcpConnection>;
using ActiveConnectionCollectionPtr = std::unique_ptr<ActiveConnections>;

// The mixin that handles the composition type ActiveConnectionCollection. This mixin
// provides the connection removal helper and the filter chain removal helper.
// All the prod stream listeners should inherit from this class and leave ActiveStreamListenerBase
// for unit test only.
class OwnedActiveStreamListenerBase : public ActiveStreamListenerBase {
public:
  OwnedActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                                Network::ListenerPtr&& listener, Network::ListenerConfig& config)
      : ActiveStreamListenerBase(parent, dispatcher, std::move(listener), config) {}

  /**
   * Remove and destroy an active connection.
   * @param connection supplies the connection to remove.
   */
  void removeConnection(ActiveTcpConnection& connection);

protected:
  /**
   * Return the active connections container attached to the given filter chain.
   */
  ActiveConnections& getOrCreateActiveConnections(const Network::FilterChain& filter_chain);

  /**
   * Remove an filter chain. All the active connections that are attached to the filter chain will
   * be destroyed.
   * @param filter_chain supplies the filter chain to remove.
   */
  void removeFilterChain(const Network::FilterChain* filter_chain) override;

  absl::flat_hash_map<const Network::FilterChain*, ActiveConnectionCollectionPtr>
      connections_by_context_;
};
} // namespace Server
} // namespace Envoy
