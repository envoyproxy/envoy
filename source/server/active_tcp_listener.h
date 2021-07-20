#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/timespan.h"

#include "source/common/common/linked_object.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/server/active_listener_base.h"

namespace Envoy {
namespace Server {

struct ActiveTcpConnection;
using ActiveTcpConnectionPtr = std::unique_ptr<ActiveTcpConnection>;
struct ActiveTcpSocket;
using ActiveTcpSocketPtr = std::unique_ptr<ActiveTcpSocket>;
class ActiveConnections;
using ActiveConnectionsPtr = std::unique_ptr<ActiveConnections>;

namespace {
// Structure used to allow a unique_ptr to be captured in a posted lambda. See below.
struct RebalancedSocket {
  Network::ConnectionSocketPtr socket;
};
using RebalancedSocketSharedPtr = std::shared_ptr<RebalancedSocket>;
} // namespace

/**
 * Wrapper for an active tcp listener owned by this handler.
 */
class ActiveTcpListener final : public Network::TcpListenerCallbacks,
                                public ActiveListenerImplBase,
                                public Network::BalancedConnectionHandler,
                                Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveTcpListener(Network::TcpConnectionHandler& parent, Network::ListenerConfig& config,
                    uint32_t worker_index);
  ActiveTcpListener(Network::TcpConnectionHandler& parent, Network::ListenerPtr&& listener,
                    Network::ListenerConfig& config);
  ~ActiveTcpListener() override;
  bool listenerConnectionLimitReached() const {
    // TODO(tonya11en): Delegate enforcement of per-listener connection limits to overload
    // manager.
    return !config_->openConnections().canCreate();
  }

  void decNumConnections() {
    ASSERT(num_listener_connections_ > 0);
    --num_listener_connections_;
    config_->openConnections().dec();
  }

  // Network::TcpListenerCallbacks
  void onAccept(Network::ConnectionSocketPtr&& socket) override;
  void onReject(RejectCause) override;

  // ActiveListenerImplBase
  Network::Listener* listener() override { return listener_.get(); }
  void pauseListening() override;
  void resumeListening() override;
  void shutdownListener() override { listener_.reset(); }

  // Network::BalancedConnectionHandler
  uint64_t numConnections() const override { return num_listener_connections_; }
  void incNumConnections() override {
    ++num_listener_connections_;
    config_->openConnections().inc();
  }
  void post(Network::ConnectionSocketPtr&& socket) override;
  void onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                      bool hand_off_restored_destination_connections, bool rebalanced) override;

  /**
   * Remove and destroy an active connection.
   * @param connection supplies the connection to remove.
   */
  void removeConnection(ActiveTcpConnection& connection);

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  void newConnection(Network::ConnectionSocketPtr&& socket,
                     std::unique_ptr<StreamInfo::StreamInfo> stream_info);

  /**
   * Return the active connections container attached with the given filter chain.
   */
  ActiveConnections& getOrCreateActiveConnections(const Network::FilterChain& filter_chain);

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

  Network::TcpConnectionHandler& parent_;
  Network::ListenerPtr listener_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::list<ActiveTcpSocketPtr> sockets_;
  absl::flat_hash_map<const Network::FilterChain*, ActiveConnectionsPtr> connections_by_context_;

  // The number of connections currently active on this listener. This is typically used for
  // connection balancing across per-handler listeners.
  std::atomic<uint64_t> num_listener_connections_{};
  bool is_deleting_{false};
};

/**
 * Wrapper for a group of active connections which are attached to the same filter chain context.
 */
class ActiveConnections : public Event::DeferredDeletable {
public:
  ActiveConnections(ActiveTcpListener& listener, const Network::FilterChain& filter_chain);
  ~ActiveConnections() override;

  // listener filter chain pair is the owner of the connections
  ActiveTcpListener& listener_;
  const Network::FilterChain& filter_chain_;
  // Owned connections
  std::list<ActiveTcpConnectionPtr> connections_;
};

/**
 * Wrapper for an active TCP connection owned by this handler.
 */
struct ActiveTcpConnection : LinkedObject<ActiveTcpConnection>,
                             public Event::DeferredDeletable,
                             public Network::ConnectionCallbacks {
  ActiveTcpConnection(ActiveConnections& active_connections,
                      Network::ConnectionPtr&& new_connection, TimeSource& time_system,
                      std::unique_ptr<StreamInfo::StreamInfo>&& stream_info);
  ~ActiveTcpConnection() override;

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
  ActiveConnections& active_connections_;
  Network::ConnectionPtr connection_;
  Stats::TimespanPtr conn_length_;
};

/**
 * Wrapper for an active accepted TCP socket owned by this handler.
 */
struct ActiveTcpSocket : public Network::ListenerFilterManager,
                         public Network::ListenerFilterCallbacks,
                         LinkedObject<ActiveTcpSocket>,
                         public Event::DeferredDeletable,
                         Logger::Loggable<Logger::Id::conn_handler> {
  ActiveTcpSocket(ActiveTcpListener& listener, Network::ConnectionSocketPtr&& socket,
                  bool hand_off_restored_destination_connections)
      : listener_(listener), socket_(std::move(socket)),
        hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
        iter_(accept_filters_.end()),
        stream_info_(std::make_unique<StreamInfo::StreamInfoImpl>(
            listener_.parent_.dispatcher().timeSource(), socket_->addressProviderSharedPtr(),
            StreamInfo::FilterState::LifeSpan::Connection)) {
    listener_.stats_.downstream_pre_cx_active_.inc();
  }
  ~ActiveTcpSocket() override {
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
  Event::Dispatcher& dispatcher() override { return listener_.parent_.dispatcher(); }
  void continueFilterChain(bool success) override;
  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override;
  envoy::config::core::v3::Metadata& dynamicMetadata() override {
    return stream_info_->dynamicMetadata();
  };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override {
    return stream_info_->dynamicMetadata();
  };

  StreamInfo::FilterState& filterState() override { return *stream_info_->filterState().get(); }

  ActiveTcpListener& listener_;
  Network::ConnectionSocketPtr socket_;
  const bool hand_off_restored_destination_connections_;
  std::list<ListenerFilterWrapperPtr> accept_filters_;
  std::list<ListenerFilterWrapperPtr>::iterator iter_;
  Event::TimerPtr timer_;
  std::unique_ptr<StreamInfo::StreamInfo> stream_info_;
  bool connected_{false};
};
using ActiveTcpListenerOptRef = absl::optional<std::reference_wrapper<ActiveTcpListener>>;

} // namespace Server
} // namespace Envoy
