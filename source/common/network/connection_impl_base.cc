#include "source/common/network/connection_impl_base.h"

namespace Envoy {
namespace Network {

void ConnectionImplBase::addIdToHashKey(std::vector<uint8_t>& hash_key, uint64_t connection_id) {
  // Pack the connection_id into sizeof(connection_id) uint8_t entries in the hash_key vector.
  hash_key.reserve(hash_key.size() + sizeof(connection_id));
  for (unsigned i = 0; i < sizeof(connection_id); ++i) {
    hash_key.push_back(0xFF & (connection_id >> (8 * i)));
  }
}

ConnectionImplBase::ConnectionImplBase(Event::Dispatcher& dispatcher, uint64_t id)
    : dispatcher_(dispatcher), id_(id) {}

void ConnectionImplBase::addConnectionCallbacks(ConnectionCallbacks& cb) {
  callbacks_.push_back(&cb);
}

void ConnectionImplBase::removeConnectionCallbacks(ConnectionCallbacks& callbacks) {
  // For performance/safety reasons we just clear the callback and do not resize the list
  for (auto& callback : callbacks_) {
    if (callback == &callbacks) {
      callback = nullptr;
      return;
    }
  }
}

OptRef<const StreamInfo::StreamInfo> ConnectionImplBase::trackedStream() const {
  return streamInfo();
}

void ConnectionImplBase::hashKey(std::vector<uint8_t>& hash) const { addIdToHashKey(hash, id()); }

void ConnectionImplBase::setConnectionStats(const ConnectionStats& stats) {
  connection_stats_ = std::make_unique<ConnectionStats>(stats);
}

void ConnectionImplBase::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  // Validate that this is only called prior to issuing a close() or closeSocket().
  ASSERT(delayed_close_timer_ == nullptr && state() == State::Open);
  delayed_close_timeout_ = timeout;
}

void ConnectionImplBase::initializeDelayedCloseTimer() {
  const auto timeout = delayed_close_timeout_.count();
  ASSERT(delayed_close_timer_ == nullptr && timeout > 0);
  delayed_close_timer_ = dispatcher_.createTimer([this]() -> void { onDelayedCloseTimeout(); });
  ENVOY_CONN_LOG(debug, "setting delayed close timer with timeout {} ms", *this, timeout);
  delayed_close_timer_->enableTimer(delayed_close_timeout_);
}

void ConnectionImplBase::raiseConnectionEvent(ConnectionEvent event) {
  for (ConnectionCallbacks* callback : callbacks_) {
    // If a previous connected callback closed the connection, don't raise any further connected
    // events. There was already recursion raising closed events. We still raise closed events
    // to further callbacks because such events are typically used for cleanup.
    if (event != ConnectionEvent::LocalClose && event != ConnectionEvent::RemoteClose &&
        state() != State::Open) {
      return;
    }

    if (callback != nullptr) {
      callback->onEvent(event);
    }
  }
}

void ConnectionImplBase::onDelayedCloseTimeout() {
  delayed_close_timer_.reset();
  ENVOY_CONN_LOG(debug, "triggered delayed close", *this);
  if (connection_stats_ != nullptr && connection_stats_->delayed_close_timeouts_ != nullptr) {
    connection_stats_->delayed_close_timeouts_->inc();
  }
  closeConnectionImmediatelyWithDetails(
      StreamInfo::LocalCloseReasons::get().TriggeredDelayedCloseTimeout);
}

} // namespace Network
} // namespace Envoy
