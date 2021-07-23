#include "source/server/active_tcp_socket.h"

#include "envoy/network/filter.h"

#include "source/common/stream_info/stream_info_impl.h"
#include "source/server/active_stream_listener_base.h"

namespace Envoy {
namespace Server {

ActiveTcpSocket::ActiveTcpSocket(ActiveStreamListenerBase& listener,
                                 Network::ConnectionSocketPtr&& socket,
                                 bool hand_off_restored_destination_connections)
    : listener_(listener), socket_(std::move(socket)),
      hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
      iter_(accept_filters_.end()),
      stream_info_(std::make_unique<StreamInfo::StreamInfoImpl>(
          listener_.dispatcher().timeSource(), socket_->addressProviderSharedPtr(),
          StreamInfo::FilterState::LifeSpan::Connection)) {
  listener_.stats_.downstream_pre_cx_active_.inc();
}

ActiveTcpSocket::~ActiveTcpSocket() {
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

Event::Dispatcher& ActiveTcpSocket::dispatcher() { return listener_.dispatcher(); }

void ActiveTcpSocket::onTimeout() {
  listener_.stats_.downstream_pre_cx_timeout_.inc();
  ASSERT(inserted());
  ENVOY_LOG(debug, "listener filter times out after {} ms",
            listener_.listener_filters_timeout_.count());

  if (listener_.continue_on_listener_filters_timeout_) {
    ENVOY_LOG(debug, "fallback to default listener filter");
    newConnection();
  }
  unlink();
}

void ActiveTcpSocket::startTimer() {
  if (listener_.listener_filters_timeout_.count() > 0) {
    timer_ = listener_.dispatcher().createTimer([this]() -> void { onTimeout(); });
    timer_->enableTimer(listener_.listener_filters_timeout_);
  }
}

void ActiveTcpSocket::unlink() {
  auto removed = listener_.removeSocket(std::move(*this));
  if (removed->timer_ != nullptr) {
    removed->timer_->disableTimer();
  }
  // Emit logs if a connection is not established.
  if (!connected_ && stream_info_ != nullptr) {
    ActiveStreamListenerBase::emitLogs(*listener_.config_, *stream_info_);
  }
  listener_.dispatcher().deferredDelete(std::move(removed));
}

void ActiveTcpSocket::continueFilterChain(bool success) {
  if (success) {
    bool no_error = true;
    if (iter_ == accept_filters_.end()) {
      iter_ = accept_filters_.begin();
    } else {
      iter_ = std::next(iter_);
    }

    for (; iter_ != accept_filters_.end(); iter_++) {
      Network::FilterStatus status = (*iter_)->onAccept(*this);
      if (status == Network::FilterStatus::StopIteration) {
        // The filter is responsible for calling us again at a later time to continue the filter
        // chain from the next filter.
        if (!socket().ioHandle().isOpen()) {
          // break the loop but should not create new connection
          no_error = false;
          break;
        } else {
          // Blocking at the filter but no error
          return;
        }
      }
    }
    // Successfully ran all the accept filters.
    if (no_error) {
      newConnection();
    } else {
      // Signal the caller that no extra filter chain iteration is needed.
      iter_ = accept_filters_.end();
    }
  }

  // Filter execution concluded, unlink and delete this ActiveTcpSocket if it was linked.
  if (inserted()) {
    unlink();
  }
}

void ActiveTcpSocket::setDynamicMetadata(const std::string& name,
                                         const ProtobufWkt::Struct& value) {
  stream_info_->setDynamicMetadata(name, value);
}

void ActiveTcpSocket::newConnection() {
  connected_ = true;

  // Check if the socket may need to be redirected to another listener.
  Network::BalancedConnectionHandlerOptRef new_listener;

  if (hand_off_restored_destination_connections_ &&
      socket_->addressProvider().localAddressRestored()) {
    // Find a listener associated with the original destination address.
    new_listener =
        listener_.getBalancedHandlerByAddress(*socket_->addressProvider().localAddress());
  }
  if (new_listener.has_value()) {
    // Hands off connections redirected by iptables to the listener associated with the
    // original destination address. Pass 'hand_off_restored_destination_connections' as false to
    // prevent further redirection.
    // Leave the new listener to decide whether to execute re-balance.
    // Note also that we must account for the number of connections properly across both listeners.
    // TODO(mattklein123): See note in ~ActiveTcpSocket() related to making this accounting better.
    listener_.decNumConnections();
    new_listener.value().get().onAcceptWorker(std::move(socket_), false, false);
  } else {
    // Set default transport protocol if none of the listener filters did it.
    if (socket_->detectedTransportProtocol().empty()) {
      socket_->setDetectedTransportProtocol("raw_buffer");
    }
    // Reset the file events which are registered by listener filter.
    // reference https://github.com/envoyproxy/envoy/issues/8925.
    socket_->ioHandle().resetFileEvents();
    accept_filters_.clear();
    // Create a new connection on this listener.
    listener_.newConnection(std::move(socket_), std::move(stream_info_));
  }
}
} // namespace Server
} // namespace Envoy
