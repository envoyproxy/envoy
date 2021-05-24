#pragma once

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

#include "server/active_listener_base.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

struct ActiveStreamSocket;

class ActiveStreamListenerBase : public ActiveListenerImplBase {
public:
  ActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                           Network::ListenerPtr&& listener, Network::ListenerConfig& config);
  static void emitLogs(Network::ListenerConfig& config, StreamInfo::StreamInfo& stream_info);

  virtual void incNumConnections() PURE;
  virtual void decNumConnections() PURE;
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  virtual void newConnection(Network::ConnectionSocketPtr&& socket,
                             std::unique_ptr<StreamInfo::StreamInfo> stream_info) PURE;

  virtual Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) PURE;

  Network::ConnectionHandler& parent_;
  Event::Dispatcher& dispatcher_;
  Network::ListenerPtr listener_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::list<std::unique_ptr<ActiveStreamSocket>> sockets_;
};

template <typename ActiveStreamType,
          typename ActiveConnectionCollectionType = typename ActiveStreamType::CollectionType>
class TypedActiveStreamListenerBase : public ActiveStreamListenerBase {

public:
  TypedActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                                Network::ListenerPtr&& listener, Network::ListenerConfig& config)
      : ActiveStreamListenerBase(parent, dispatcher, std::move(listener), config) {}
  using ActiveGenericConnectionPtr = std::unique_ptr<ActiveStreamType>;
  using ActiveGenericConnectionsPtr = std::unique_ptr<ActiveConnectionCollectionType>;

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

  absl::node_hash_map<const Network::FilterChain*, ActiveGenericConnectionsPtr>
      connections_by_context_;
  bool is_deleting_{false};
};

/**
 * Wrapper for an active accepted socket owned by the active listener.
 */
struct ActiveStreamSocket : public Network::ListenerFilterManager,
                            public Network::ListenerFilterCallbacks,
                            LinkedObject<ActiveStreamSocket>,
                            public Event::DeferredDeletable,
                            Logger::Loggable<Logger::Id::conn_handler> {
  ActiveStreamSocket(ActiveStreamListenerBase& listener, Network::ConnectionSocketPtr&& socket,
                     bool hand_off_restored_destination_connections)
      : listener_(listener), socket_(std::move(socket)),
        hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
        iter_(accept_filters_.end()),
        stream_info_(std::make_unique<StreamInfo::StreamInfoImpl>(
            listener_.dispatcher().timeSource(), socket_->addressProviderSharedPtr(),
            StreamInfo::FilterState::LifeSpan::Connection)) {
    listener_.stats_.downstream_pre_cx_active_.inc();
  }
  ~ActiveStreamSocket() override {
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
  Event::Dispatcher& dispatcher() override { return listener_.dispatcher(); }
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

using ActiveInternalSocket = ActiveStreamSocket;

} // namespace Server
} // namespace Envoy
