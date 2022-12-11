#include "source/common/http/conn_pool_grid.h"

#include <cstdint>

#include "source/common/http/http3_status_tracker_impl.h"
#include "source/common/http/mixed_conn_pool.h"

#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/quic_versions.h"

namespace Envoy {
namespace Http {

namespace {
absl::string_view describePool(const ConnectionPool::Instance& pool) {
  return pool.protocolDescription();
}

static constexpr uint32_t kDefaultTimeoutMs = 300;

std::string getSni(const Network::TransportSocketOptionsConstSharedPtr& options,
                   Network::UpstreamTransportSocketFactory& transport_socket_factory) {
  if (options && options->serverNameOverride().has_value()) {
    return options->serverNameOverride().value();
  }
  return std::string(transport_socket_factory.defaultServerNameIndication());
}

} // namespace

ConnectivityGrid::WrapperCallbacks::WrapperCallbacks(ConnectivityGrid& grid,
                                                     Http::ResponseDecoder& decoder,
                                                     PoolIterator pool_it,
                                                     ConnectionPool::Callbacks& callbacks,
                                                     const Instance::StreamOptions& options)
    : grid_(grid), decoder_(decoder), inner_callbacks_(&callbacks),
      next_attempt_timer_(
          grid_.dispatcher_.createTimer([this]() -> void { tryAnotherConnection(); })),
      current_(pool_it), stream_options_(options) {
  if (!stream_options_.can_use_http3_) {
    // If alternate protocols are explicitly disabled, there must have been a failed request over
    // HTTP/3 and the failure must be post-handshake. So disable HTTP/3 for this request.
    http3_attempt_failed_ = true;
  }
}

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
  ASSERT(!parent_.grid_.isPoolHttp3(pool()) || parent_.stream_options_.can_use_http3_);
  auto* cancellable = pool().newStream(parent_.decoder_, *this, parent_.stream_options_);
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
            describePool(**current_), grid_.origin_.hostname_);
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
    Upstream::HostDescriptionConstSharedPtr host, StreamInfo::StreamInfo& info,
    absl::optional<Http::Protocol> protocol) {
  ENVOY_LOG(trace, "{} pool successfully connected to host '{}'.", describePool(attempt->pool()),
            host->hostname());
  if (!grid_.isPoolHttp3(attempt->pool())) {
    tcp_attempt_succeeded_ = true;
    maybeMarkHttp3Broken();
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
    ENVOY_LOG(trace, "Marking HTTP/3 broken for host '{}'.", grid_.origin_.hostname_);
    grid_.markHttp3Broken();
  }
}

void ConnectivityGrid::WrapperCallbacks::ConnectionAttemptCallbacks::onPoolReady(
    RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
    StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol) {
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
    HttpServerPropertiesCacheSharedPtr alternate_protocols,
    ConnectivityOptions connectivity_options, Quic::QuicStatNames& quic_stat_names,
    Stats::Scope& scope, Http::PersistentQuicInfo& quic_info)
    : dispatcher_(dispatcher), random_generator_(random_generator), host_(host),
      priority_(priority), options_(options), transport_socket_options_(transport_socket_options),
      state_(state), next_attempt_duration_(std::chrono::milliseconds(kDefaultTimeoutMs)),
      time_source_(time_source), alternate_protocols_(alternate_protocols),
      quic_stat_names_(quic_stat_names), scope_(scope),
      // TODO(RyanTheOptimist): Figure out how scheme gets plumbed in here.
      origin_("https", getSni(transport_socket_options, host_->transportSocketFactory()),
              host_->address()->ip()->port()),
      quic_info_(quic_info) {
  // ProdClusterManagerFactory::allocateConnPool verifies the protocols are HTTP/1, HTTP/2 and
  // HTTP/3.
  ASSERT(connectivity_options.protocols_.size() == 3);
  ASSERT(alternate_protocols);
  std::chrono::milliseconds rtt =
      std::chrono::duration_cast<std::chrono::milliseconds>(alternate_protocols_->getSrtt(origin_));
  if (rtt.count() != 0) {
    next_attempt_duration_ = std::chrono::milliseconds(rtt.count() * 2);
  }
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
    pool = Http3::allocateConnPool(
        dispatcher_, random_generator_, host_, priority_, options_, transport_socket_options_,
        state_, quic_stat_names_, *alternate_protocols_, scope_,
        makeOptRefFromPtr<Http3::PoolConnectResultCallback>(this), quic_info_);
  } else {
    pool = std::make_unique<HttpConnPoolImplMixed>(dispatcher_, random_generator_, host_, priority_,
                                                   options_, transport_socket_options_, state_,
                                                   origin_, alternate_protocols_);
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
                                                         ConnectionPool::Callbacks& callbacks,
                                                         const Instance::StreamOptions& options) {
  ASSERT(!deferred_deleting_);

  // New streams should not be created during draining.
  ASSERT(!draining_);

  if (pools_.empty()) {
    createNextPool();
  }
  PoolIterator pool = pools_.begin();
  Instance::StreamOptions overriding_options = options;
  bool delay_tcp_attempt = true;
  if (shouldAttemptHttp3() && options.can_use_http3_) {
    if (getHttp3StatusTracker().hasHttp3FailedRecently()) {
      overriding_options.can_send_early_data_ = false;
      delay_tcp_attempt = false;
    }
  } else {
    ASSERT(options.can_use_http3_ ||
           Runtime::runtimeFeatureEnabled(Runtime::conn_pool_new_stream_with_early_data_and_http3));

    // Before skipping to the next pool, make sure it has been created.
    createNextPool();
    ++pool;
  }
  auto wrapped_callback =
      std::make_unique<WrapperCallbacks>(*this, decoder, pool, callbacks, overriding_options);
  ConnectionPool::Cancellable* ret = wrapped_callback.get();
  LinkedList::moveIntoList(std::move(wrapped_callback), wrapped_callbacks_);
  if (wrapped_callbacks_.front()->newStream() == StreamCreationResult::ImmediateResult) {
    // If newStream succeeds, return nullptr as the caller has received their
    // callback and does not need a cancellable handle. At this point the
    // WrappedCallbacks object has also been deleted.
    return nullptr;
  }
  if (!delay_tcp_attempt) {
    // Immediately start TCP attempt if HTTP/3 failed recently.
    wrapped_callbacks_.front()->tryAnotherConnection();
  }
  return ret;
}

void ConnectivityGrid::addIdleCallback(IdleCb cb) {
  // Add the callback to the list of callbacks to be called when all drains are
  // complete.
  idle_callbacks_.emplace_back(cb);
}

void ConnectivityGrid::drainConnections(Envoy::ConnectionPool::DrainBehavior drain_behavior) {
  if (draining_) {
    // A drain callback has already been set, and only needs to happen once.
    return;
  }

  if (drain_behavior == Envoy::ConnectionPool::DrainBehavior::DrainAndDelete) {
    // Note that no new pools can be created from this point on
    // as createNextPool fast-fails if `draining_` is true.
    draining_ = true;
  }

  for (auto& pool : pools_) {
    pool->drainConnections(drain_behavior);
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

HttpServerPropertiesCache::Http3StatusTracker& ConnectivityGrid::getHttp3StatusTracker() const {
  ENVOY_BUG(host_->address()->type() == Network::Address::Type::Ip, "Address is not an IP address");
  return alternate_protocols_->getOrCreateHttp3StatusTracker(origin_);
}

bool ConnectivityGrid::isHttp3Broken() const { return getHttp3StatusTracker().isHttp3Broken(); }

void ConnectivityGrid::markHttp3Broken() {
  host_->cluster().trafficStats().upstream_http3_broken_.inc();
  getHttp3StatusTracker().markHttp3Broken();
}

void ConnectivityGrid::markHttp3Confirmed() { getHttp3StatusTracker().markHttp3Confirmed(); }

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
  if (host_->address()->type() != Network::Address::Type::Ip) {
    IS_ENVOY_BUG("Address is not an IP address");
    return false;
  }
  uint32_t port = host_->address()->ip()->port();
  OptRef<const std::vector<HttpServerPropertiesCache::AlternateProtocol>> protocols =
      alternate_protocols_->findAlternatives(origin_);
  if (!protocols.has_value()) {
    ENVOY_LOG(trace, "No alternate protocols available for host '{}', skipping HTTP/3.",
              origin_.hostname_);
    return false;
  }
  if (isHttp3Broken()) {
    ENVOY_LOG(trace, "HTTP/3 is broken to host '{}', skipping.", host_->hostname());
    return false;
  }
  for (const HttpServerPropertiesCache::AlternateProtocol& protocol : protocols.ref()) {
    // TODO(RyanTheOptimist): Handle alternate protocols which change hostname or port.
    if (!protocol.hostname_.empty() || protocol.port_ != port) {
      ENVOY_LOG(trace,
                "Alternate protocol for host '{}' attempts to change host or port, skipping.",
                origin_.hostname_);
      continue;
    }

    // TODO(RyanTheOptimist): Cache this mapping, but handle the supported versions list
    // changing dynamically.
    spdy::SpdyAltSvcWireFormat::AlternativeService alt_svc(protocol.alpn_, protocol.hostname_,
                                                           protocol.port_, 0, {});
    quic::ParsedQuicVersion version = quic::SpdyUtils::ExtractQuicVersionFromAltSvcEntry(
        alt_svc, quic::CurrentSupportedVersions());
    if (version != quic::ParsedQuicVersion::Unsupported()) {
      // TODO(RyanTheOptimist): Pass this version down to the HTTP/3 pool.
      ENVOY_LOG(trace, "HTTP/3 advertised for host '{}'", origin_.hostname_);
      return true;
    }

    ENVOY_LOG(trace, "Alternate protocol for host '{}' has unsupported ALPN '{}', skipping.",
              origin_.hostname_, protocol.alpn_);
  }

  ENVOY_LOG(trace, "HTTP/3 is not available to host '{}', skipping.", origin_.hostname_);
  return false;
}

void ConnectivityGrid::onHandshakeComplete() {
  ENVOY_LOG(trace, "Marking HTTP/3 confirmed for host '{}'.", origin_.hostname_);
  markHttp3Confirmed();
}

void ConnectivityGrid::onZeroRttHandshakeFailed() {
  ENVOY_LOG(trace, "Marking HTTP/3 failed for host '{}'.", host_->hostname());
  ASSERT(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_sends_early_data"));
  getHttp3StatusTracker().markHttp3FailedRecently();
}

} // namespace Http
} // namespace Envoy
