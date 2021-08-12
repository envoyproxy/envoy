#include "source/common/http/conn_pool_grid.h"

#include "source/common/http/http3/conn_pool.h"
#include "source/common/http/mixed_conn_pool.h"

#include "quiche/quic/core/quic_versions.h"

namespace Envoy {
namespace Http {

namespace {
absl::string_view describePool(const ConnectionPool::Instance& pool) {
  return pool.protocolDescription();
}
} // namespace

ConnectivityGrid::WrapperCallbacks::WrapperCallbacks(ConnectivityGrid& grid,
                                                     Http::ResponseDecoder& decoder,
                                                     PoolIterator pool_it,
                                                     ConnectionPool::Callbacks& callbacks)
    : grid_(grid), decoder_(decoder), inner_callbacks_(&callbacks),
      next_attempt_timer_(
          grid_.dispatcher_.createTimer([this]() -> void { tryAnotherConnection(); })),
      current_(pool_it) {}

// TODO(#15649) add trace logging.
ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::ConnectionAttemptCallbacks(
    WrapperCallbacks& parent, PoolIterator it)
    : parent_(parent), pool_it_(it), cancellable_(nullptr) {}

ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::~ConnectionAttemptCallbacks() {
  if (cancellable_ != nullptr) {
    cancel(Envoy::ConnectionPool::CancelPolicy::Default);
  }
}

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
  cancellable_ = nullptr; // Attempt failed and can no longer be cancelled.
  parent_.onConnectionAttemptFailed(this, reason, transport_failure_reason, host);
}

void ConnectivityGrid::WrapperCallbacks::onConnectionAttemptFailed(
    ConnectionAttemptCallbacks* attempt, ConnectionPool::PoolFailureReason reason,
    absl::string_view transport_failure_reason, Upstream::HostDescriptionConstSharedPtr host) {
  ASSERT(host == grid_.host_);
  ENVOY_LOG(trace, "{} pool failed to create connection to host '{}'.",
            describePool(attempt->pool()), host->hostname());
  if (grid_.isPoolHttp3(attempt->pool())) {
    http3_attempt_failed_ = true;
  }
  maybeMarkHttp3Broken();

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
  // original caller, if the caller hasn't already been notified.
  ConnectionPool::Callbacks* callbacks = inner_callbacks_;
  inner_callbacks_ = nullptr;
  deleteThis();
  if (callbacks != nullptr) {
    ENVOY_LOG(trace, "Passing pool failure up to caller.", describePool(attempt->pool()),
              host->hostname());
    callbacks->onPoolFailure(reason, transport_failure_reason, host);
  }
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
  ASSERT(host == grid_.host_);
  ENVOY_LOG(trace, "{} pool successfully connected to host '{}'.", describePool(attempt->pool()),
            host->hostname());
  if (!grid_.isPoolHttp3(attempt->pool())) {
    tcp_attempt_succeeded_ = true;
    maybeMarkHttp3Broken();
  } else {
    ENVOY_LOG(trace, "Marking HTTP/3 confirmed for host '{}'.", grid_.host_->hostname());
    grid_.markHttp3Confirmed();
  }

  auto delete_this_on_return = attempt->removeFromList(connection_attempts_);
  ConnectionPool::Callbacks* callbacks = inner_callbacks_;
  inner_callbacks_ = nullptr;
  // If an HTTP/3 connection attempts is in progress, let it complete so that if it succeeds
  // it can be used for future requests. But if there is a TCP connection attempt in progress,
  // cancel it.
  if (grid_.isPoolHttp3(attempt->pool())) {
    cancelAllPendingAttempts(Envoy::ConnectionPool::CancelPolicy::Default);
  }
  if (connection_attempts_.empty()) {
    deleteThis();
  }
  if (callbacks != nullptr) {
    callbacks->onPoolReady(encoder, host, info, protocol);
  }
}

void ConnectivityGrid::WrapperCallbacks::maybeMarkHttp3Broken() {
  if (http3_attempt_failed_ && tcp_attempt_succeeded_) {
    ENVOY_LOG(trace, "Marking HTTP/3 broken for host '{}'.", grid_.host_->hostname());
    grid_.markHttp3Broken();
  }
}

void ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::onPoolReady(
    RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
    const StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol) {
  cancellable_ = nullptr; // Attempt succeeded and can no longer be cancelled.
  parent_.onConnectionAttemptReady(this, encoder, host, info, protocol);
}

void ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::cancel(
    Envoy::ConnectionPool::CancelPolicy cancel_policy) {
  auto cancellable = cancellable_;
  cancellable_ = nullptr; // Prevent repeated cancellations.
  cancellable->cancel(cancel_policy);
}

void ConnectivityGrid::WrapperCallbacks::cancel(Envoy::ConnectionPool::CancelPolicy cancel_policy) {
  // If the newStream caller cancels the stream request, pass the cancellation on
  // to each connection attempt.
  cancelAllPendingAttempts(cancel_policy);
  deleteThis();
}

void ConnectivityGrid::WrapperCallbacks::cancelAllPendingAttempts(
    Envoy::ConnectionPool::CancelPolicy cancel_policy) {
  for (auto& attempt : connection_attempts_) {
    attempt->cancel(cancel_policy);
  }
  connection_attempts_.clear();
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
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Upstream::ClusterConnectivityState& state, TimeSource& time_source,
    AlternateProtocolsCacheSharedPtr alternate_protocols,
    std::chrono::milliseconds next_attempt_duration, ConnectivityOptions connectivity_options,
    Quic::QuicStatNames& quic_stat_names, Stats::Scope& scope)
    : dispatcher_(dispatcher), random_generator_(random_generator), host_(host),
      priority_(priority), options_(options), transport_socket_options_(transport_socket_options),
      state_(state), next_attempt_duration_(next_attempt_duration), time_source_(time_source),
      http3_status_tracker_(dispatcher_), alternate_protocols_(alternate_protocols),
      quic_stat_names_(quic_stat_names), scope_(scope) {
  // ProdClusterManagerFactory::allocateConnPool verifies the protocols are HTTP/1, HTTP/2 and
  // HTTP/3.
  // TODO(#15649) support v6/v4, WiFi/cellular.
  ASSERT(connectivity_options.protocols_.size() == 3);
}

ConnectivityGrid::~ConnectivityGrid() {
  // Ignore idle callbacks while the pools are destroyed below.
  destroying_ = true;
  // Callbacks might have pending streams registered with the pools, so cancel and delete
  // the callback before deleting the pools.
  wrapped_callbacks_.clear();
  pools_.clear();
}

void ConnectivityGrid::deleteIsPending() {
  deferred_deleting_ = true;
  for (const auto& pool : pools_) {
    pool->deleteIsPending();
  }
}

absl::optional<ConnectivityGrid::PoolIterator> ConnectivityGrid::createNextPool() {
  ASSERT(!deferred_deleting_);
  // Pools are created by newStream, which should not be called during draining.
  ASSERT(!draining_);
  // Right now, only H3 and TCP are supported, so if there are 2 pools we're done.
  if (pools_.size() == 2 || draining_) {
    return absl::nullopt;
  }

  // HTTP/3 is hard-coded as higher priority, H2 as secondary.
  ConnectionPool::InstancePtr pool;
  if (pools_.empty()) {
    pool = Http3::allocateConnPool(dispatcher_, random_generator_, host_, priority_, options_,
                                   transport_socket_options_, state_, time_source_,
                                   quic_stat_names_, scope_);
  } else {
    pool = std::make_unique<HttpConnPoolImplMixed>(dispatcher_, random_generator_, host_, priority_,
                                                   options_, transport_socket_options_, state_);
  }

  setupPool(*pool);
  pools_.push_back(std::move(pool));

  return --pools_.end();
}

void ConnectivityGrid::setupPool(ConnectionPool::Instance& pool) {
  pool.addIdleCallback([this]() { onIdleReceived(); });
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
  ASSERT(!deferred_deleting_);

  // New streams should not be created during draining.
  ASSERT(!draining_);

  if (pools_.empty()) {
    createNextPool();
  }
  PoolIterator pool = pools_.begin();
  if (!shouldAttemptHttp3()) {
    // Before skipping to the next pool, make sure it has been created.
    createNextPool();
    ++pool;
  }
  auto wrapped_callback = std::make_unique<WrapperCallbacks>(*this, decoder, pool, callbacks);
  ConnectionPool::Cancellable* ret = wrapped_callback.get();
  LinkedList::moveIntoList(std::move(wrapped_callback), wrapped_callbacks_);
  if (wrapped_callbacks_.front()->newStream() == StreamCreationResult::ImmediateResult) {
    // If newStream succeeds, return nullptr as the caller has received their
    // callback and does not need a cancellable handle. At this point the
    // WrappedCallbacks object has also been deleted.
    return nullptr;
  }
  return ret;
}

void ConnectivityGrid::addIdleCallback(IdleCb cb) {
  // Add the callback to the list of callbacks to be called when all drains are
  // complete.
  idle_callbacks_.emplace_back(cb);
}

void ConnectivityGrid::startDrain() {
  if (draining_) {
    // A drain callback has already been set, and only needs to happen once.
    return;
  }

  // Note that no new pools can be created from this point on
  // as createNextPool fast-fails if `draining_` is true.
  draining_ = true;

  for (auto& pool : pools_) {
    pool->startDrain();
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

bool ConnectivityGrid::isPoolHttp3(const ConnectionPool::Instance& pool) {
  return &pool == pools_.begin()->get();
}

bool ConnectivityGrid::isHttp3Broken() const { return http3_status_tracker_.isHttp3Broken(); }

void ConnectivityGrid::markHttp3Broken() { http3_status_tracker_.markHttp3Broken(); }

void ConnectivityGrid::markHttp3Confirmed() { http3_status_tracker_.markHttp3Confirmed(); }

bool ConnectivityGrid::isIdle() const {
  // This is O(n) but the function is constant and there are no plans for n > 8.
  bool idle = true;
  for (const auto& pool : pools_) {
    idle &= pool->isIdle();
  }
  return idle;
}

void ConnectivityGrid::onIdleReceived() {
  // Don't do any work under the stack of ~ConnectivityGrid()
  if (destroying_) {
    return;
  }

  if (isIdle()) {
    for (auto& callback : idle_callbacks_) {
      callback();
    }
  }
}

bool ConnectivityGrid::shouldAttemptHttp3() {
  if (http3_status_tracker_.isHttp3Broken()) {
    ENVOY_LOG(trace, "HTTP/3 is broken to host '{}', skipping.", host_->hostname());
    return false;
  }
  if (!alternate_protocols_) {
    ENVOY_LOG(trace, "No alternate protocols cache. Attempting HTTP/3 to host '{}'.",
              host_->hostname());
    return true;
  }
  if (host_->address()->type() != Network::Address::Type::Ip) {
    ENVOY_LOG(error, "Address is not an IP address");
    ASSERT(false);
    return false;
  }
  uint32_t port = host_->address()->ip()->port();
  // TODO(RyanTheOptimist): Figure out how scheme gets plumbed in here.
  AlternateProtocolsCache::Origin origin("https", host_->hostname(), port);
  OptRef<const std::vector<AlternateProtocolsCache::AlternateProtocol>> protocols =
      alternate_protocols_->findAlternatives(origin);
  if (!protocols.has_value()) {
    ENVOY_LOG(trace, "No alternate protocols available for host '{}', skipping HTTP/3.",
              host_->hostname());
    return false;
  }

  for (const AlternateProtocolsCache::AlternateProtocol& protocol : protocols.ref()) {
    // TODO(RyanTheOptimist): Handle alternate protocols which change hostname or port.
    if (!protocol.hostname_.empty() || protocol.port_ != port) {
      ENVOY_LOG(trace,
                "Alternate protocol for host '{}' attempts to change host or port, skipping.",
                host_->hostname());
      continue;
    }

    // TODO(RyanTheOptimist): Cache this mapping, but handle the supported versions list
    // changing dynamically.
    for (const quic::ParsedQuicVersion& version : quic::CurrentSupportedVersions()) {
      if (quic::AlpnForVersion(version) == protocol.alpn_) {
        // TODO(RyanTheOptimist): Pass this version down to the HTTP/3 pool.
        ENVOY_LOG(trace, "HTTP/3 advertised for host '{}'", host_->hostname());
        return true;
      }
    }

    ENVOY_LOG(trace, "Alternate protocol for host '{}' has unsupported ALPN '{}', skipping.",
              host_->hostname(), protocol.alpn_);
  }

  ENVOY_LOG(trace, "HTTP/3 is not available to host '{}', skipping.", host_->hostname());
  return false;
}

} // namespace Http
} // namespace Envoy
