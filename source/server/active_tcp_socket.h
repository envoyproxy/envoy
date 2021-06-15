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
#include "source/server/active_listener_base.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

class ActiveStreamListenerBase;

/**
 * Wrapper for an active accepted socket owned by the active tcp listener.
 */
struct ActiveTcpSocket : public Network::ListenerFilterManager,
                         public Network::ListenerFilterCallbacks,
                         LinkedObject<ActiveTcpSocket>,
                         public Event::DeferredDeletable,
                         Logger::Loggable<Logger::Id::conn_handler> {
  ActiveTcpSocket(ActiveStreamListenerBase& listener, Network::ConnectionSocketPtr&& socket,
                  bool hand_off_restored_destination_connections);
  ~ActiveTcpSocket() override;

  void onTimeout();
  void startTimer();
  void unlink();
  void newConnection();

  class GenericListenerFilter : public Network::ListenerFilter {
  public:
    GenericListenerFilter(const Network::ListenerFilterMatcherSharedPtr& matcher,
                          Network::ListenerFilterPtr listener_filter)
        : listener_filter_(std::move(listener_filter)), matcher_(std::move(matcher)) {}
    Network::FilterStatus onAccept(ListenerFilterCallbacks& cb) override {
      if (isDisabled(cb)) {
        return Network::FilterStatus::Continue;
      }
      return listener_filter_->onAccept(cb);
    }
    /**
     * Check if this filter filter should be disabled on the incoming socket.
     * @param cb the callbacks the filter instance can use to communicate with the filter chain.
     **/
    bool isDisabled(ListenerFilterCallbacks& cb) {
      if (matcher_ == nullptr) {
        return false;
      } else {
        return matcher_->matches(cb);
      }
    }

  private:
    const Network::ListenerFilterPtr listener_filter_;
    const Network::ListenerFilterMatcherSharedPtr matcher_;
  };
  using ListenerFilterWrapperPtr = std::unique_ptr<GenericListenerFilter>;

  // Network::ListenerFilterManager
  void addAcceptFilter(const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                       Network::ListenerFilterPtr&& filter) override {
    accept_filters_.emplace_back(
        std::make_unique<GenericListenerFilter>(listener_filter_matcher, std::move(filter)));
  }

  // Network::ListenerFilterCallbacks
  Network::ConnectionSocket& socket() override { return *socket_.get(); }
  Event::Dispatcher& dispatcher() override;
  void continueFilterChain(bool success) override;
  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override;
  envoy::config::core::v3::Metadata& dynamicMetadata() override {
    return stream_info_->dynamicMetadata();
  };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override {
    return stream_info_->dynamicMetadata();
  };

  StreamInfo::FilterState& filterState() override { return *stream_info_->filterState().get(); }

  ActiveStreamListenerBase& listener_;
  Network::ConnectionSocketPtr socket_;
  const bool hand_off_restored_destination_connections_;
  std::list<ListenerFilterWrapperPtr> accept_filters_;
  std::list<ListenerFilterWrapperPtr>::iterator iter_;
  Event::TimerPtr timer_;
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  bool connected_{false};
};

class ActiveStreamListenerBase : public ActiveListenerImplBase {
public:
  ActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                           Network::ListenerPtr&& listener, Network::ListenerConfig& config);
  static void emitLogs(Network::ListenerConfig& config, StreamInfo::StreamInfo& stream_info);

  virtual void incNumConnections() PURE;
  virtual void decNumConnections() PURE;

  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  virtual void newConnection(Network::ConnectionSocketPtr&& socket,
                             std::unique_ptr<StreamInfo::StreamInfo> stream_info) PURE;

  virtual Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) PURE;

  void onSocketAccepted(std::unique_ptr<ActiveTcpSocket> active_socket) {
    // Create and run the filters
    config_->filterChainFactory().createListenerFilterChain(*active_socket);
    active_socket->continueFilterChain(true);

    // Move active_socket to the sockets_ list if filter iteration needs to continue later.
    // Otherwise we let active_socket be destructed when it goes out of scope.
    if (active_socket->iter_ != active_socket->accept_filters_.end()) {
      active_socket->startTimer();
      LinkedList::moveIntoListBack(std::move(active_socket), sockets_);
    } else {
      if (!active_socket->connected_) {
        // If active_socket is about to be destructed, emit logs if a connection is not created.
        if (active_socket->stream_info_ != nullptr) {
          emitLogs(*config_, *active_socket->stream_info_);
        } else {
          // If the active_socket is not connected, this socket is not promoted to active
          // connection. Thus the stream_info_ is owned by this active socket.
          ENVOY_BUG(active_socket->stream_info_ != nullptr,
                    "the unconnected active socket must have stream info.");
        }
      }
    }
  }

  Network::ConnectionHandler& parent_;
  Event::Dispatcher& dispatcher_;
  Network::ListenerPtr listener_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::list<std::unique_ptr<ActiveTcpSocket>> sockets_;
};

template <typename ActiveConnectionType>
class TypedActiveStreamListenerBase : public ActiveStreamListenerBase {

public:
  using ActiveConnectionCollectionType = typename ActiveConnectionType::CollectionType;
  TypedActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                                Network::ListenerPtr&& listener, Network::ListenerConfig& config)
      : ActiveStreamListenerBase(parent, dispatcher, std::move(listener), config) {}
  using ActiveConnectionPtr = std::unique_ptr<ActiveConnectionType>;
  using ActiveConnectionsPtr = std::unique_ptr<ActiveConnectionCollectionType>;

  /**
   * Schedule to remove and destroy the active connections which are not tracked by listener
   * config. Caution: The connection are not destroyed yet when function returns.
   */
  void
  deferredRemoveFilterChains(const std::list<const Network::FilterChain*>& draining_filter_chains) {
    // Need to recover the original deleting state.
    const bool was_deleting = is_deleting_;
    is_deleting_ = true;
    for (const auto* filter_chain : draining_filter_chains) {
      auto iter = connections_by_context_.find(filter_chain);
      if (iter == connections_by_context_.end()) {
        // It is possible when listener is stopping.
      } else {
        auto& connections = iter->second->connections_;
        while (!connections.empty()) {
          connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
        }
        // Since is_deleting_ is on, we need to manually remove the map value and drive the
        // iterator. Defer delete connection container to avoid race condition in destroying
        // connection.
        dispatcher().deferredDelete(std::move(iter->second));
        connections_by_context_.erase(iter);
      }
    }
    is_deleting_ = was_deleting;
  }

protected:
  void cleanupConnections() {
    is_deleting_ = true;

    // Purge sockets that have not progressed to connections. This should only happen when
    // a listener filter stops iteration and never resumes.
    while (!sockets_.empty()) {
      auto removed = sockets_.front()->removeFromList(sockets_);
      dispatcher().deferredDelete(std::move(removed));
    }

    for (auto& chain_and_connections : connections_by_context_) {
      ASSERT(chain_and_connections.second != nullptr);
      auto& connections = chain_and_connections.second->connections_;
      while (!connections.empty()) {
        connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
      }
    }
    dispatcher().clearDeferredDeleteList();
  }

  absl::node_hash_map<const Network::FilterChain*, ActiveConnectionsPtr> connections_by_context_;
  bool is_deleting_{false};
};

} // namespace Server
} // namespace Envoy
