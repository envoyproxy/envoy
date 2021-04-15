#include "common/http/conn_pool_grid.h"

#include "common/http/http3/conn_pool.h"
#include "common/http/mixed_conn_pool.h"

namespace Envoy {
namespace Http {

// Helper function to make sure each protocol in expected_protocols is present
// in protocols (only used for an ASSERT in debug builds)
bool contains(const std::vector<Http::Protocol>& protocols,
              const std::vector<Http::Protocol>& expected_protocols) {
  for (auto protocol : expected_protocols) {
    if (std::find(protocols.begin(), protocols.end(), protocol) == protocols.end()) {
      return false;
    }
  }
  return true;
}

absl::string_view describePool(const ConnectionPool::Instance& pool) {
  return pool.protocolDescription();
}

ConnectivityGrid::WrapperCallbacks::WrapperCallbacks(ConnectivityGrid& grid,
                                                     Http::ResponseDecoder& decoder,
                                                     PoolIterator pool_it,
                                                     ConnectionPool::Callbacks& callbacks)
    : grid_(grid), decoder_(decoder), inner_callbacks_(callbacks),
      next_attempt_timer_(
          grid_.dispatcher_.createTimer([this]() -> void { tryAnotherConnection(); })),
      current_(pool_it) {}

// TODO(#15649) add trace logging.
ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::ConnectionAttemptCallbacks(
    WrapperCallbacks& parent, PoolIterator it)
    : parent_(parent), pool_it_(it), cancellable_(nullptr) {}

ConnectivityGrid::StreamCreationResult
ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::newStream() {
  auto* cancellable = pool().newStream(parent_.decoder_, *this);
  if (cancellable == nullptr) {
    return StreamCreationResult::ImmediateResult;
  }
  cancellable_ = cancellable;
  return StreamCreationResult::StreamCreationPending;
}

void ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::onPoolFailure(
    ConnectionPool::PoolFailureReason reason, absl::string_view transport_failure_reason,
    Upstream::HostDescriptionConstSharedPtr host) {
  parent_.onConnectionAttemptFailed(this, reason, transport_failure_reason, host);
}

void ConnectivityGrid::WrapperCallbacks::onConnectionAttemptFailed(
    ConnectionAttemptCallbacks* attempt, ConnectionPool::PoolFailureReason reason,
    absl::string_view transport_failure_reason, Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(trace, "{} pool failed to create connection to host '{}'.",
            describePool(attempt->pool()), grid_.host_->hostname());
  auto delete_this_on_return = attempt->removeFromList(connection_attempts_);

  // If there is another connection attempt in flight then let that proceed.
  if (!connection_attempts_.empty()) {
    return;
  }

  // If the next connection attempt does not immediately fail, let it proceed.
  if (tryAnotherConnection()) {
    return;
  }

  // If this point is reached, all pools have been tried. Pass the pool failure up to the
  // original caller.
  ConnectionPool::Callbacks& callbacks = inner_callbacks_;
  ENVOY_LOG(trace, "Passing pool failure up to caller.", describePool(attempt->pool()),
            grid_.host_->hostname());
  deleteThis();
  callbacks.onPoolFailure(reason, transport_failure_reason, host);
}

void ConnectivityGrid::WrapperCallbacks::deleteThis() {
  // By removing the entry from the list, it will be deleted.
  removeFromList(grid_.wrapped_callbacks_);
}

ConnectivityGrid::StreamCreationResult ConnectivityGrid::WrapperCallbacks::newStream() {
  ENVOY_LOG(trace, "{} pool attempting to create a new stream to host '{}'.",
            describePool(**current_), grid_.host_->hostname());
  auto attempt = std::make_unique<ConnectionAttemptCallbacks>(*this, current_);
  LinkedList::moveIntoList(std::move(attempt), connection_attempts_);
  if (!next_attempt_timer_->enabled()) {
    next_attempt_timer_->enableTimer(grid_.next_attempt_duration_);
  }
  // Note that in the case of immediate attempt/failure, newStream will delete this.
  return connection_attempts_.front()->newStream();
}

void ConnectivityGrid::WrapperCallbacks::onConnectionAttemptReady(
    ConnectionAttemptCallbacks* attempt, RequestEncoder& encoder,
    Upstream::HostDescriptionConstSharedPtr host, const StreamInfo::StreamInfo& info,
    absl::optional<Http::Protocol> protocol) {
  ENVOY_LOG(trace, "{} pool successfully connected to host '{}'.", describePool(attempt->pool()),
            grid_.host_->hostname());
  auto delete_on_return = attempt->removeFromList(connection_attempts_);
  // The first successful connection is passed up, and all others will be canceled.
  // TODO: Ensure that if HTTP/2 succeeds, we can allow the HTTP/3 connection to run to completion.
  for (auto& attempt : connection_attempts_) {
    attempt->cancel(Envoy::ConnectionPool::CancelPolicy::Default);
  }
  ConnectionPool::Callbacks& callbacks = inner_callbacks_;
  deleteThis();
  return callbacks.onPoolReady(encoder, host, info, protocol);
}

void ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::onPoolReady(
    RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
    const StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol) {
  parent_.onConnectionAttemptReady(this, encoder, host, info, protocol);
}

void ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::cancel(
    Envoy::ConnectionPool::CancelPolicy cancel_policy) {
  cancellable_->cancel(cancel_policy);
}

void ConnectivityGrid::WrapperCallbacks::cancel(Envoy::ConnectionPool::CancelPolicy cancel_policy) {
  // If the newStream caller cancels the stream request, pass the cancellation on
  // to each connection attempt.
  for (auto& attempt : connection_attempts_) {
    attempt->cancel(cancel_policy);
  }
  deleteThis();
}

bool ConnectivityGrid::WrapperCallbacks::tryAnotherConnection() {
  absl::optional<PoolIterator> next_pool = grid_.nextPool(current_);
  if (!next_pool.has_value()) {
    // If there are no other pools to try, return false.
    return false;
  }
  // Create a new connection attempt for the next pool. If we reach this point
  // return true regardless of if newStream resulted in an immediate result or
  // an async call, as either way the attempt will result in success/failure
  // callbacks.
  current_ = next_pool.value();
  newStream();
  return true;
}

ConnectivityGrid::ConnectivityGrid(
    Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
    Upstream::ClusterConnectivityState& state, TimeSource& time_source,
    std::chrono::milliseconds next_attempt_duration, ConnectivityOptions connectivity_options)
    : dispatcher_(dispatcher), random_generator_(random_generator), host_(host),
      priority_(priority), options_(options), transport_socket_options_(transport_socket_options),
      state_(state), next_attempt_duration_(next_attempt_duration), time_source_(time_source) {
  // TODO(#15649) support v6/v4, WiFi/cellular.
  ASSERT(connectivity_options.protocols_.size() == 3);
  ASSERT(contains(connectivity_options.protocols_,
                  {Http::Protocol::Http11, Http::Protocol::Http2, Http::Protocol::Http3}));
}

ConnectivityGrid::~ConnectivityGrid() {
  // Ignore drained callbacks while the pools are destroyed below.
  destroying_ = true;
  pools_.clear();
}

absl::optional<ConnectivityGrid::PoolIterator> ConnectivityGrid::createNextPool() {
  // Pools are created by newStream, which should not be called during draining.
  ASSERT(drained_callbacks_.empty());
  // Right now, only H3 and ALPN are supported, so if there are 2 pools we're done.
  if (pools_.size() == 2 || !drained_callbacks_.empty()) {
    return absl::nullopt;
  }

  // HTTP/3 is hard-coded as higher priority, H2 as secondary.
  if (pools_.empty()) {
    pools_.push_back(Http3::allocateConnPool(dispatcher_, random_generator_, host_, priority_,
                                             options_, transport_socket_options_, state_,
                                             time_source_));
    return pools_.begin();
  }
  pools_.push_back(std::make_unique<HttpConnPoolImplMixed>(dispatcher_, random_generator_, host_,
                                                           priority_, options_,
                                                           transport_socket_options_, state_));
  return std::next(pools_.begin());
}

bool ConnectivityGrid::hasActiveConnections() const {
  // This is O(n) but the function is constant and there are no plans for n > 8.
  for (const auto& pool : pools_) {
    if (pool->hasActiveConnections()) {
      return true;
    }
  }
  return false;
}

ConnectionPool::Cancellable* ConnectivityGrid::newStream(Http::ResponseDecoder& decoder,
                                                         ConnectionPool::Callbacks& callbacks) {
  if (pools_.empty()) {
    createNextPool();
  }

  // TODO(#15649) track pools with successful connections: don't always start at
  // the front of the list.
  auto wrapped_callback =
      std::make_unique<WrapperCallbacks>(*this, decoder, pools_.begin(), callbacks);
  ConnectionPool::Cancellable* ret = wrapped_callback.get();
  LinkedList::moveIntoList(std::move(wrapped_callback), wrapped_callbacks_);
  // Note that in the case of immediate attempt/failure, newStream will delete this.
  if (wrapped_callbacks_.front()->newStream() == StreamCreationResult::ImmediateResult) {
    // If newStream succeeds, return nullptr as the caller has received their
    // callback and does not need a cancellable handle.
    return nullptr;
  }
  return ret;
}

void ConnectivityGrid::addDrainedCallback(DrainedCb cb) {
  // Add the callback to the list of callbacks to be called when all drains are
  // complete.
  drained_callbacks_.emplace_back(cb);

  if (drained_callbacks_.size() != 1) {
    return;
  }

  // If this is the first time a drained callback has been added, track the
  // number of pools which need to be drained in order to pass drain-completion
  // up to the callers. Note that no new pools can be created from this point on
  // as createNextPool fast-fails if drained callbacks are present.
  drains_needed_ = pools_.size();
  for (auto& pool : pools_) {
    pool->addDrainedCallback([this]() -> void { onDrainReceived(); });
  }
}

void ConnectivityGrid::drainConnections() {
  for (auto& pool : pools_) {
    pool->drainConnections();
  }
}

Upstream::HostDescriptionConstSharedPtr ConnectivityGrid::host() const { return host_; }

bool ConnectivityGrid::maybePreconnect(float) {
  return false; // Preconnect not yet supported for the grid.
}

absl::optional<ConnectivityGrid::PoolIterator> ConnectivityGrid::nextPool(PoolIterator pool_it) {
  pool_it++;
  if (pool_it != pools_.end()) {
    return pool_it;
  }
  return createNextPool();
}

void ConnectivityGrid::onDrainReceived() {
  // Don't do any work under the stack of ~ConnectivityGrid()
  if (destroying_) {
    return;
  }

  // If not all the pools have drained, keep waiting.
  ASSERT(drains_needed_ != 0);
  if (--drains_needed_ != 0) {
    return;
  }

  // All the pools have drained. Notify drain subscribers.
  for (auto& callback : drained_callbacks_) {
    callback();
  }
}

} // namespace Http
} // namespace Envoy
