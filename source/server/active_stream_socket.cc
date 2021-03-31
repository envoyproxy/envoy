#include "server/active_stream_socket.h"

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/stats/timespan_impl.h"

#include "server/connection_handler_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Server {

ActiveStreamListenerBase::ActiveStreamListenerBase(Network::ConnectionHandler& parent,
                                                   Event::Dispatcher& dispatcher,
                                                   Network::ListenerPtr&& listener,
                                                   Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(parent, &config), parent_(parent),
      dispatcher_(dispatcher), listener_(std::move(listener)),
      listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()) {}

void ActiveStreamListenerBase::emitLogs(Network::ListenerConfig& config,
                                        StreamInfo::StreamInfo& stream_info) {
  stream_info.onRequestComplete();
  for (const auto& access_log : config.accessLogs()) {
    access_log->log(nullptr, nullptr, nullptr, stream_info);
  }
}

void ActiveStreamSocket::onTimeout() {
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

void ActiveStreamSocket::startTimer() {
  if (listener_.listener_filters_timeout_.count() > 0) {
    timer_ = listener_.dispatcher().createTimer([this]() -> void { onTimeout(); });
    timer_->enableTimer(listener_.listener_filters_timeout_);
  }
}

void ActiveStreamSocket::unlink() {
  auto removed = removeFromList(listener_.sockets_);
  if (removed->timer_ != nullptr) {
    removed->timer_->disableTimer();
  }
  // Emit logs if a connection is not established.
  if (!connected_) {
    ActiveStreamListenerBase::emitLogs(*listener_.config_, *stream_info_);
  }
  listener_.dispatcher().deferredDelete(std::move(removed));
}

void ActiveStreamSocket::continueFilterChain(bool success) {
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

void ActiveStreamSocket::setDynamicMetadata(const std::string& name,
                                            const ProtobufWkt::Struct& value) {
  stream_info_->setDynamicMetadata(name, value);
}

void ActiveStreamSocket::newConnection() {
  connected_ = true;
  listener_.incNumConnections();
  // Set default transport protocol if none of the listener filters did it.
  if (socket_->detectedTransportProtocol().empty()) {
    socket_->setDetectedTransportProtocol(
        Extensions::TransportSockets::TransportProtocolNames::get().RawBuffer);
  }
  // TODO(lambdai): add integration test
  // TODO: Address issues in wider scope. See https://github.com/envoyproxy/envoy/issues/8925
  // Erase accept filter states because accept filters may not get the opportunity to clean up.
  // Particularly the assigned events need to reset before assigning new events in the follow up.
  accept_filters_.clear();
  // Create a new connection on this listener.
  listener_.newConnection(std::move(socket_), std::move(stream_info_));
}

} // namespace Server
} // namespace Envoy