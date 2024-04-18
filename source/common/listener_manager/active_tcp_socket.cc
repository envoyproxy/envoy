#include "source/common/listener_manager/active_tcp_socket.h"

#include "envoy/network/filter.h"

#include "source/common/listener_manager/active_stream_listener_base.h"
#include "source/common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Server {

ActiveTcpSocket::ActiveTcpSocket(ActiveStreamListenerBase& listener,
                                 Network::ConnectionSocketPtr&& socket,
                                 bool hand_off_restored_destination_connections)
    : listener_(listener), socket_(std::move(socket)),
      hand_off_restored_destination_connections_(hand_off_restored_destination_connections),
      iter_(accept_filters_.end()),
      stream_info_(std::make_unique<StreamInfo::StreamInfoImpl>(
          listener_.dispatcher().timeSource(), socket_->connectionInfoProviderSharedPtr(),
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

void ActiveTcpSocket::createListenerFilterBuffer() {
  listener_filter_buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
      socket_->ioHandle(), listener_.dispatcher(),
      [this](bool error) {
        socket_->ioHandle().close();
        if (error) {
          listener_.stats_.downstream_listener_filter_error_.inc();
        } else {
          listener_.stats_.downstream_listener_filter_remote_close_.inc();
        }
        continueFilterChain(false);
      },
      [this](Network::ListenerFilterBufferImpl& filter_buffer) {
        ASSERT((*iter_)->maxReadBytes() != 0);
        Network::FilterStatus status = (*iter_)->onData(filter_buffer);
        if (status == Network::FilterStatus::StopIteration) {
          if (socket_->ioHandle().isOpen()) {
            // The listener filter should not wait for more data when it has already received
            // all the data it requested.
            ASSERT(filter_buffer.rawSlice().len_ < (*iter_)->maxReadBytes());
            // Check if the maxReadBytes is changed or not. If change,
            // reset the buffer capacity.
            if ((*iter_)->maxReadBytes() > filter_buffer.capacity()) {
              filter_buffer.resetCapacity((*iter_)->maxReadBytes());
              // Activate `Read` event manually in case the data already
              // available in the socket buffer.
              filter_buffer.activateFileEvent(Event::FileReadyType::Read);
            }
          } else {
            // The filter closed the socket.
            continueFilterChain(false);
          }
          return;
        }
        continueFilterChain(true);
      },
      (*iter_)->maxReadBytes());
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
          // Break the loop but should not create new connection.
          no_error = false;
          break;
        } else {
          // If the listener maxReadBytes() is 0, then it shouldn't return
          // `FilterStatus::StopIteration` from `onAccept` to wait for more data.
          ASSERT((*iter_)->maxReadBytes() != 0);
          if (listener_filter_buffer_ == nullptr) {
            if ((*iter_)->maxReadBytes() > 0) {
              createListenerFilterBuffer();
            }
          } else {
            // If the current filter expect more data than previous filters, then
            // increase the filter buffer's capacity.
            if (listener_filter_buffer_->capacity() < (*iter_)->maxReadBytes()) {
              listener_filter_buffer_->resetCapacity((*iter_)->maxReadBytes());
            }
          }
          if (listener_filter_buffer_ != nullptr) {
            // There are two cases for activate event manually: One is
            // the data is already available when connect, activate the read event to peek
            // data from the socket . Another one is the data already
            // peeked into the buffer when previous filter processing the data, then activate the
            // read event to trigger the current filter callback to process the data.
            listener_filter_buffer_->activateFileEvent(Event::FileReadyType::Read);
          }
          // Waiting for more data.
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
      socket_->connectionInfoProvider().localAddressRestored()) {
    // Find a listener associated with the original destination address.
    new_listener =
        listener_.getBalancedHandlerByAddress(*socket_->connectionInfoProvider().localAddress());
  }

  // Reset the file events which are registered by listener filter.
  // reference https://github.com/envoyproxy/envoy/issues/8925.
  if (listener_filter_buffer_ != nullptr) {
    listener_filter_buffer_->reset();
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
    accept_filters_.clear();
    // Create a new connection on this listener.
    listener_.newConnection(std::move(socket_), std::move(stream_info_));
  }
}
} // namespace Server
} // namespace Envoy
