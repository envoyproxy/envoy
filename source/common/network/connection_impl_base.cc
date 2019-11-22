#include "common/network/connection_impl_base.h"

namespace Envoy {
namespace Network {

ConnectionImplBase::ConnectionImplBase(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher), id_(next_global_id_++) {}

std::atomic<uint64_t> ConnectionImplBase::next_global_id_;

void ConnectionImplBase::addConnectionCallbacks(ConnectionCallbacks& cb) {
  callbacks_.push_back(&cb);
}

void ConnectionImplBase::setConnectionStats(const ConnectionStats& stats) {
  ASSERT(!connection_stats_,
         "Two network filters are attempting to set connection stats. This indicates an issue "
         "with the configured filter chain.");
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

void ConnectionImplBase::onDelayedCloseTimeout() {
  delayed_close_timer_.reset();
  ENVOY_CONN_LOG(debug, "triggered delayed close", *this);
  if (connection_stats_ != nullptr && connection_stats_->delayed_close_timeouts_ != nullptr) {
    connection_stats_->delayed_close_timeouts_->inc();
  }
  closeConnectionImmediately();
}

} // namespace Network
} // namespace Envoy
