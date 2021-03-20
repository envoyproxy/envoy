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
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/common/linked_object.h"
#include "common/common/non_copyable.h"
#include "common/stream_info/stream_info_impl.h"

#include "server/connection_handler_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

struct ActiveInternalConnection;
using ActiveInternalConnectionPtr = std::unique_ptr<ActiveInternalConnection>;
struct ActiveInternalSocket;
using ActiveInternalSocketPtr = std::unique_ptr<ActiveInternalSocket>;
class ActiveInternalConnections;
using ActiveInternalConnectionsPtr = std::unique_ptr<ActiveInternalConnections>;

class ActiveInternalListener : public ConnectionHandlerImpl::ActiveListenerImplBase,
                               public Network::InternalListenerCallbacks,
                               Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveInternalListener(Network::ConnectionHandler& conn_handler, Event::Dispatcher& dispatcher,
                         Network::ListenerPtr&& listener, Network::ListenerConfig& config);
  ~ActiveInternalListener() override;
  // ActiveListenerImplBase
  Network::Listener* listener() override { return listener_.get(); }
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

  void decNumConnections() {
    // FIX-ME: redesign openConnections.
    // config_->openConnections().dec();
  }
  /**
   * Remove and destroy an active connection.
   * @param connection supplies the connection to remove.
   */
  void removeConnection(ActiveInternalConnection& connection);

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  void newConnection(Network::ConnectionSocketPtr&& socket,
                     std::unique_ptr<StreamInfo::StreamInfo> stream_info);

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

  Network::ConnectionHandler& parent_;
  Event::Dispatcher& dispatcher_;
  Network::ListenerPtr listener_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::list<ActiveInternalSocketPtr> sockets_;
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

/**
 * Wrapper for an active accepted TCP socket owned by this handler.
 */
struct ActiveInternalSocket : public Network::ListenerFilterManager,
                              public Network::ListenerFilterCallbacks,
                              LinkedObject<ActiveInternalSocket>,
                              public Event::DeferredDeletable,
                              Logger::Loggable<Logger::Id::conn_handler> {
  ActiveInternalSocket(ActiveInternalListener& listener, Network::ConnectionSocketPtr&& socket,
                       bool hand_off_restored_destination_connections)
      : listener_(listener), socket_(std::move(socket)),
        hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
        iter_(accept_filters_.end()),
        stream_info_(std::make_unique<StreamInfo::StreamInfoImpl>(
            listener_.dispatcher().timeSource(), socket_->addressProviderSharedPtr(),
            StreamInfo::FilterState::LifeSpan::Connection)) {
    listener_.stats_.downstream_pre_cx_active_.inc();
  }
  ~ActiveInternalSocket() override {
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

  ActiveInternalListener& listener_;
  Network::ConnectionSocketPtr socket_;
  const bool hand_off_restored_destination_connections_;
  std::list<ListenerFilterWrapperPtr> accept_filters_;
  std::list<ListenerFilterWrapperPtr>::iterator iter_;
  Event::TimerPtr timer_;
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  bool connected_{false};
};
} // namespace Server
} // namespace Envoy