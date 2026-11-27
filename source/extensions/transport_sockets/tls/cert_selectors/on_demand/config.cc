#include "source/extensions/transport_sockets/tls/cert_selectors/on_demand/config.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/event/timer.h"
#include "envoy/network/transport_socket.h"
#include "envoy/router/string_accessor.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "source/common/common/fair_sieve_cache.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/context_impl.h"
#include "source/server/generic_factory_context.h"

#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

namespace {

FairSieve::TenantId extractTenantIdFromSsl(const SSL* ssl) {
  if (ssl == nullptr) {
    return FairSieve::kDefaultTenantId;
  }
  auto* callbacks = static_cast<Network::TransportSocketCallbacks*>(
      SSL_get_ex_data(ssl, ContextImpl::sslSocketIndex()));
  if (callbacks == nullptr) {
    return FairSieve::kDefaultTenantId;
  }
  const auto& filter_state = callbacks->connection().streamInfo().filterState();
  if (filter_state == nullptr) {
    return FairSieve::kDefaultTenantId;
  }
  const auto* tenant =
      filter_state->getDataReadOnly<Router::StringAccessor>(TenantIdFilterStateKey);
  return tenant != nullptr ? FairSieve::computeTenantId(tenant->asString())
                           : FairSieve::kDefaultTenantId;
}

FairSieve::TenantId
extractTenantIdFromOptionsOrSsl(const SSL& ssl,
                                const Network::TransportSocketOptionsConstSharedPtr& options) {
  if (options) {
    for (const auto& obj : options->downstreamSharedFilterStateObjects()) {
      if (obj.name_ == TenantIdFilterStateKey) {
        if (const auto* value = dynamic_cast<const Router::StringAccessor*>(obj.data_.get());
            value != nullptr) {
          return FairSieve::computeTenantId(value->asString());
        }
        break;
      }
    }
  }
  return extractTenantIdFromSsl(&ssl);
}

} // namespace

AsyncContextConfig::AsyncContextConfig(absl::string_view cert_name,
                                       Server::Configuration::ServerFactoryContext& factory_context,
                                       const envoy::config::core::v3::ConfigSource& config_source,
                                       OptRef<Init::Manager> init_manager, UpdateCb update_cb,
                                       RemoveCb remove_cb)
    : factory_context_(factory_context), cert_name_(cert_name),
      cert_provider_(factory_context_.secretManager().findOrCreateTlsCertificateProvider(
          config_source, cert_name_, factory_context_, init_manager, false)),
      update_cb_(update_cb),
      update_cb_handle_(cert_provider_->addUpdateCallback([this]() { return loadCert(); })),
      remove_cb_(remove_cb), remove_cb_handle_(cert_provider_->addRemoveCallback(
                                 [this]() { return remove_cb_(cert_name_); })) {}

absl::Status AsyncContextConfig::loadCert() {
  // Called on main, possibly during the constructor.
  auto* secret = cert_provider_->secret();
  if (secret != nullptr) {
    Server::GenericFactoryContextImpl generic_context(factory_context_,
                                                      factory_context_.messageValidationVisitor());
    auto config_or_error = Ssl::TlsCertificateConfigImpl::create(
        *secret, generic_context, factory_context_.api(), cert_name_);
    RETURN_IF_NOT_OK(config_or_error.status());
    cert_config_.emplace(*std::move(config_or_error));
    return update_cb_(cert_name_, *cert_config_);
  }
  return absl::OkStatus();
}

void Handle::notify(AsyncContextConstSharedPtr cert_ctx) {
  ASSERT(cb_);
  bool staple = false;
  if (cert_ctx) {
    active_context_ = cert_ctx;
    staple =
        (ocspStapleAction(active_context_->tlsContext(), client_ocsp_capable_,
                          active_context_->ocspStaplePolicy()) == Ssl::OcspStapleAction::Staple);
  }
  Event::Dispatcher& dispatcher = cb_->dispatcher();
  // TODO: This could benefit from batching events by the dispatcher in the outer loop.
  dispatcher.post([cb = std::move(cb_), cert_ctx, staple] {
    cb->onCertificateSelectionResult(
        makeOptRefFromPtr(cert_ctx ? &cert_ctx->tlsContext() : nullptr), staple);
  });
  cb_ = nullptr;
}

CertSelectionStatsSharedPtr generateCertSelectionStats(Stats::Scope& store) {
  return std::make_shared<CertSelectionStats>(CertSelectionStats{
      ALL_CERT_SELECTION_STATS(POOL_COUNTER(store), POOL_GAUGE(store), POOL_HISTOGRAM(store))});
}

SecretManager::SecretManager(const ConfigProto& config,
                             Server::Configuration::GenericFactoryContext& factory_context,
                             AsyncContextFactory&& context_factory)
    : stats_scope_(factory_context.scope().createScope("on_demand_secret.")),
      stats_(generateCertSelectionStats(*stats_scope_)),
      factory_context_(factory_context.serverFactoryContext()),
      config_source_(config.config_source()), context_factory_(std::move(context_factory)),
      policy_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_active_secrets, 0),
              PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_pending_secrets, 0)),
      cert_contexts_(factory_context_.threadLocal()) {
  cert_contexts_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCerts>(); });
  if (policy_.pendingLimitEnabled()) {
    reclaim_timer_ = factory_context_.mainThreadDispatcher().createTimer(
        [this]() { reclaimAbandonedPending(); });
  }
  if (policy_.activeLimitEnabled() && config.has_idle_timeout()) {
    // Sample idleness `kIdleSweepsPerTimeout` times per timeout so worker handshakes only set a
    // relaxed atomic flag instead of reading a clock. Populate `prefetched_names_` before starting
    // prefetch subscriptions so synchronous activation honors the exemption immediately.
    const uint64_t idle_ms = PROTOBUF_GET_MS_OR_DEFAULT(config, idle_timeout, 0);
    idle_sweep_interval_ =
        std::chrono::milliseconds(std::max<uint64_t>(1, idle_ms / kIdleSweepsPerTimeout));
    prefetched_names_.insert(config.prefetch_secret_names().begin(),
                             config.prefetch_secret_names().end());
    idle_timer_ =
        factory_context_.mainThreadDispatcher().createTimer([this]() { sweepIdleSecrets(); });
  }
  for (const auto& name : config.prefetch_secret_names()) {
    addCertificateConfig(name, nullptr, factory_context.initManager());
  }
}

SecretManager::~SecretManager() {
  if (reclaim_timer_ != nullptr) {
    reclaim_timer_->disableTimer();
  }
  if (idle_timer_ != nullptr) {
    idle_timer_->disableTimer();
  }
  // Release Accumulate gauges so replacing a filter chain (which shares the parent listener stats
  // scope in ThreadLocalStoreImpl) does not permanently leak cached/pending counts.
  size_t pending_count = 0;
  for (const auto& [name, entry] : cache_) {
    if (entry->cert_context_ == nullptr) {
      ++pending_count;
    }
  }
  stats_->cert_active_.sub(cache_.size());
  stats_->cert_pending_.sub(pending_count);
}

void SecretManager::disableReclaimTimerIfIdle() {
  if (pending_on_demand_keys_.empty() && reclaim_timer_ != nullptr && reclaim_timer_->enabled()) {
    reclaim_timer_->disableTimer();
  }
}

bool SecretManager::hasActiveNonPrefetchedSecret() const {
  if (policy_.activeCount() == 0) {
    return false;
  }
  if (prefetched_names_.empty()) {
    return true;
  }
  for (const auto& [name, entry] : cache_) {
    if (entry->cert_context_ != nullptr && !prefetched_names_.contains(name)) {
      return true;
    }
  }
  return false;
}

void SecretManager::disableIdleTimerIfIdle() {
  if (idle_timer_ == nullptr) {
    return;
  }
  if (!hasActiveNonPrefetchedSecret()) {
    if (pending_idle_removals_.capacity() > kMaxIdleRemovalsPerTick) {
      std::vector<PendingIdleRemoval>().swap(pending_idle_removals_);
    } else {
      pending_idle_removals_.clear();
    }
    if (idle_timer_->enabled()) {
      idle_timer_->disableTimer();
    }
  }
}

void SecretManager::sweepIdleSecrets() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (pending_idle_removals_.empty()) {
    std::vector<std::string> idle_names;
    policy_.collectIdle(kIdleSweepsPerTimeout, idle_names);
    pending_idle_removals_.reserve(idle_names.size());
    for (std::string& name : idle_names) {
      if (prefetched_names_.contains(name)) {
        continue;
      }
      auto it = cache_.find(name);
      if (it == cache_.end() || it->second->cert_context_ == nullptr) {
        continue;
      }
      pending_idle_removals_.push_back(
          PendingIdleRemoval{std::move(name), it->second->generation_});
    }
  }

  const size_t batch_size = std::min(kMaxIdleRemovalsPerTick, pending_idle_removals_.size());
  std::vector<std::string> removed_names;
  removed_names.reserve(batch_size);
  for (size_t i = 0; i < batch_size; ++i) {
    PendingIdleRemoval item = std::move(pending_idle_removals_.back());
    pending_idle_removals_.pop_back();
    auto it = cache_.find(item.name_);
    // ABA-safe check: skip entries already evicted/removed or superseded by a newer generation.
    if (it == cache_.end() || it->second->generation_ != item.generation_ ||
        it->second->cert_context_ == nullptr) {
      continue;
    }
    // Skip entries hit on a worker between paced drain ticks; the next scheduled sweep will
    // consume `used_since_idle_sweep_` and reset their idle counter.
    if (it->second->hit_state_ != nullptr && it->second->hit_state_->usedSinceIdleSweep()) {
      continue;
    }
    ASSERT(it->second->callbacks_.empty());
    policy_.onRemoved(item.name_);
    if (it->second->cert_config_ != nullptr) {
      factory_context_.mainThreadDispatcher().deferredDelete(std::move(it->second->cert_config_));
    }
    cache_.erase(it);
    stats_->cert_active_.dec();
    stats_->cert_idle_expired_.inc();
    removed_names.push_back(std::move(item.name_));
  }

  if (!removed_names.empty()) {
    ENVOY_LOG(debug, "Removed {} idle certificates ({} remaining in sweep)", removed_names.size(),
              pending_idle_removals_.size());
    removeContexts(std::move(removed_names));
  }

  if (!pending_idle_removals_.empty()) {
    // Yield to the main-thread event loop before draining the next batch so large post-flood
    // sweeps do not stall handshakes, SDS updates, or admin probes.
    idle_timer_->enableTimer(std::chrono::milliseconds(0));
    return;
  }
  if (pending_idle_removals_.capacity() > kMaxIdleRemovalsPerTick) {
    std::vector<PendingIdleRemoval>().swap(pending_idle_removals_);
  }
  if (hasActiveNonPrefetchedSecret()) {
    idle_timer_->enableTimer(idle_sweep_interval_);
  }
}

void SecretManager::removeContexts(std::vector<std::string>&& secret_names) {
  // One broadcast for the whole batch; the names are shared with the workers, not copied.
  auto names = std::make_shared<const std::vector<std::string>>(std::move(secret_names));
  cert_contexts_.runOnAllThreads(
      [names](OptRef<ThreadLocalCerts> certs) {
        for (const std::string& name : *names) {
          certs->ctx_by_name_.erase(name);
        }
      },
      [stats_scope = stats_scope_, stats = stats_] { stats->cert_updated_.inc(); });
}

void SecretManager::reclaimAbandonedPending() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  const MonotonicTime now = factory_context_.timeSource().monotonicTime();
  // Single O(max_pending) pass executed off the admission hot path at most once per
  // `kMinAbandonedPendingAge`, decoupling main-thread work from denied request rates while
  // preventing fast-disconnecting clients from bypassing `max_pending_secrets`.
  for (auto it = pending_on_demand_keys_.begin(); it != pending_on_demand_keys_.end();) {
    auto cache_it = cache_.find(*it);
    if (cache_it == cache_.end() || cache_it->second->cert_context_ != nullptr) {
      pending_on_demand_keys_.erase(it++);
      continue;
    }
    if ((now - cache_it->second->created_at_) < kMinAbandonedPendingAge) {
      ++it;
      continue;
    }
    auto& callbacks = cache_it->second->callbacks_;
    callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                   [](const PendingCallback& cb) { return cb.handle_.expired(); }),
                    callbacks.end());
    if (!callbacks.empty()) {
      ++it;
      continue;
    }
    // All waiting client handshakes for this non-prefetched pending fetch have disconnected and
    // the cooldown has elapsed.
    const std::string key = *it;
    pending_on_demand_keys_.erase(it++);
    policy_.onRemoved(key);
    if (cache_it->second->cert_config_ != nullptr) {
      factory_context_.mainThreadDispatcher().deferredDelete(
          std::move(cache_it->second->cert_config_));
    }
    cache_.erase(cache_it);
    stats_->cert_active_.dec();
    stats_->cert_pending_.dec();
    ENVOY_LOG(debug, "Reclaimed abandoned pending certificate subscription for '{}'", key);
  }
  if (!pending_on_demand_keys_.empty() && reclaim_timer_ != nullptr) {
    reclaim_timer_->enableTimer(kMinAbandonedPendingAge);
  }
}

void SecretManager::addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                                         OptRef<Init::Manager> init_manager,
                                         FairSieve::TenantId tenant_id) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (auto it = cache_.find(secret_name); it != cache_.end()) {
    CacheEntry& entry = *it->second;
    if (handle) {
      if (entry.cert_context_) {
        if (entry.hit_state_ != nullptr) {
          entry.hit_state_->recordHit(tenant_id);
        }
        handle->notify(entry.cert_context_);
      } else {
        // Duplicate handshakes for an already-pending secret coalesce without consuming another
        // pending slot. Compact expired waiters before appending so retries from disconnected
        // clients cannot grow `callbacks_` without bound.
        entry.callbacks_.erase(
            std::remove_if(entry.callbacks_.begin(), entry.callbacks_.end(),
                           [](const PendingCallback& cb) { return cb.handle_.expired(); }),
            entry.callbacks_.end());
        entry.callbacks_.push_back(PendingCallback{handle, tenant_id, /*is_coalesced_=*/true});
      }
    }
    return;
  }

  // Enforce Phase 1 pending admission control (Choudhury & Hahne Dynamic Thresholds) in O(1) for
  // on-demand client-triggered fetches (`handle != nullptr`). Prefetched secrets are always
  // admitted and tracked. Abandoned pending fetches are reclaimed periodically off this hot path
  // by `reclaim_timer_`.
  if (handle && !policy_.canAdmitPending(tenant_id)) {
    stats_->cert_pending_rejected_.inc();
    ENVOY_LOG(debug, "Rejected pending on-demand certificate fetch for '{}' (tenant={})",
              secret_name, tenant_id);
    handle->notify(nullptr);
    return;
  }

  auto entry = std::make_unique<CacheEntry>();
  const uint64_t generation = next_entry_generation_++;
  entry->generation_ = generation;
  entry->owner_tenant_ = tenant_id;
  entry->created_at_ = factory_context_.timeSource().monotonicTime();
  CacheEntry* entry_ptr = entry.get();
  if (handle) {
    entry_ptr->callbacks_.push_back(PendingCallback{handle, tenant_id, /*is_coalesced_=*/false});
    if (policy_.pendingLimitEnabled()) {
      pending_on_demand_keys_.emplace(secret_name);
      if (reclaim_timer_ != nullptr && !reclaim_timer_->enabled()) {
        reclaim_timer_->enableTimer(kMinAbandonedPendingAge);
      }
    }
  }
  cache_.emplace(std::string(secret_name), std::move(entry));

  // Record stats and pending policy state BEFORE constructing AsyncContextConfig because its
  // constructor can synchronously fire updateCertificate() for an already-cached SDS provider.
  // Invariants across synchronous eviction:
  //  - Self-eviction impossibility: `secret_name` is pending and not yet in `policy_`'s active
  //    set, so `evictOne()` cannot pick it as the eviction victim.
  //  - Pointer stability: `cache_` stores `unique_ptr<CacheEntry>`, so `entry_ptr` remains valid
  //    even if `updateCertificate()` erases another key from `cache_`.
  //  - Gauge conservation: `cert_requested`, `cert_active`, and `cert_pending` all increment by 1.
  stats_->cert_requested_.inc();
  stats_->cert_active_.inc();
  stats_->cert_pending_.inc();
  policy_.onPendingAdded(secret_name, tenant_id);

  entry_ptr->cert_config_ = std::make_unique<AsyncContextConfig>(
      secret_name, factory_context_, config_source_, init_manager,
      [this](absl::string_view name, const Ssl::TlsCertificateConfig& cert_config) -> absl::Status {
        return updateCertificate(name, cert_config);
      },
      [this, generation](absl::string_view name) -> absl::Status {
        return removeCertificateConfig(name, generation);
      });
}

absl::Status SecretManager::updateCertificate(absl::string_view secret_name,
                                              const Ssl::TlsCertificateConfig& cert_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto it = cache_.find(secret_name);
  if (it == cache_.end()) {
    return absl::OkStatus();
  }

  absl::Status creation_status = absl::OkStatus();
  auto cert_context =
      context_factory_(*stats_scope_, factory_context_, cert_config, creation_status);
  RETURN_IF_NOT_OK(creation_status);

  CacheEntry* entry = it->second.get();
  const bool was_pending = (entry->cert_context_ == nullptr);
  if (was_pending) {
    stats_->cert_pending_.dec();
    pending_on_demand_keys_.erase(secret_name);
    disableReclaimTimerIfIdle();
  }

  std::optional<FairSieve::Policy::EvictionResult> evicted;
  FairSieve::HitStateSharedPtr hit_state = policy_.onActivated(secret_name, evicted);
  entry->hit_state_ = hit_state;
  entry->cert_context_ = cert_context;

  std::optional<std::string> evicted_name;
  if (evicted.has_value()) {
    stats_->cert_evicted_.inc();
    if (evicted->ownership_transfers > 0) {
      stats_->cert_ownership_transferred_.add(evicted->ownership_transfers);
    }
    evicted_name = std::move(evicted->evicted_key);
    if (auto ev_it = cache_.find(*evicted_name); ev_it != cache_.end()) {
      for (const auto& pending_cb : ev_it->second->callbacks_) {
        if (auto handle = pending_cb.handle_.lock(); handle) {
          handle->notify(nullptr);
        }
      }
      // Defer destruction of the evicted SDS subscription to avoid mutating xDS watch lists
      // while an SDS discovery response callback is on the stack.
      if (ev_it->second->cert_config_ != nullptr) {
        factory_context_.mainThreadDispatcher().deferredDelete(
            std::move(ev_it->second->cert_config_));
      }
      pending_on_demand_keys_.erase(*evicted_name);
      cache_.erase(ev_it);
      stats_->cert_active_.dec();
    }
    ENVOY_LOG(debug, "Evicted certificate '{}' upon activation of '{}' (transfers={})",
              *evicted_name, secret_name, evicted->ownership_transfers);
  }
  if (idle_timer_ != nullptr) {
    if (!prefetched_names_.contains(secret_name)) {
      if (!idle_timer_->enabled()) {
        idle_timer_->enableTimer(idle_sweep_interval_);
      }
    } else if (evicted.has_value()) {
      disableIdleTimerIfIdle();
    }
  }

  // Update the future lookups (coalescing any victim removal into a single cross-thread
  // broadcast) and notify pending callbacks. Preserve hit attribution for live coalesced waiters
  // so shared resources requested by a secondary tenant while pending become eligible for lazy
  // ownership transfer without marking single-request scans as visited.
  updateContexts(secret_name, CachedCert{cert_context, std::move(hit_state)}, evicted_name);

  size_t notify_count = 0;
  for (const auto& pending_cb : entry->callbacks_) {
    if (auto handle = pending_cb.handle_.lock(); handle) {
      if (pending_cb.is_coalesced_ && entry->hit_state_ != nullptr) {
        const FairSieve::TenantId attr_tenant = (pending_cb.tenant_id_ != entry->owner_tenant_)
                                                    ? pending_cb.tenant_id_
                                                    : FairSieve::kDefaultTenantId;
        entry->hit_state_->recordHit(attr_tenant);
      }
      handle->notify(cert_context);
      notify_count++;
    }
  }
  ENVOY_LOG(trace, "Notified {} pending connections about certificate '{}', out of queued {}",
            notify_count, secret_name, entry->callbacks_.size());
  entry->callbacks_.clear();
  return absl::OkStatus();
}

absl::Status SecretManager::updateAll() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  for (auto& [secret_name, entry] : cache_) {
    // Skip entries that are still pending (`cert_context_ == nullptr`); promoting them here would
    // bypass `policy_.onActivated()`, strand waiting handles, and leak `cert_pending_`.
    if (entry->cert_context_ == nullptr || entry->cert_config_ == nullptr) {
      continue;
    }
    const auto& cert_config = entry->cert_config_->certConfig();
    // Refresh only if there is a certificate present and skip notifying.
    if (cert_config) {
      absl::Status creation_status = absl::OkStatus();
      auto new_context =
          context_factory_(*stats_scope_, factory_context_, *cert_config, creation_status);
      // Validate status BEFORE mutating `entry->cert_context_` or broadcasting to workers because
      // `std::make_shared<ServerAsyncContext>` returns a non-null pointer even on failure.
      RETURN_IF_NOT_OK(creation_status);
      entry->cert_context_ = std::move(new_context);
      updateContexts(secret_name, CachedCert{entry->cert_context_, entry->hit_state_});
    }
  }
  return absl::OkStatus();
}

absl::Status SecretManager::removeCertificateConfig(absl::string_view secret_name,
                                                    std::optional<uint64_t> expected_generation) {
  // We cannot remove the subscription caller directly because this is called during a callback
  // which continues later. Instead, we post to the main as a completion.
  factory_context_.mainThreadDispatcher().post(
      [weak_this = std::weak_ptr<SecretManager>(shared_from_this()),
       name = std::string(secret_name), expected_generation] {
        if (auto that = weak_this.lock(); that) {
          that->doRemoveCertificateConfig(name, expected_generation);
        }
      });
  return absl::OkStatus();
}

void SecretManager::doRemoveCertificateConfig(absl::string_view secret_name,
                                              std::optional<uint64_t> expected_generation) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto it = cache_.find(secret_name);
  if (it == cache_.end()) {
    return;
  }
  // Invariant (ABA-safe async removal): because `next_entry_generation_` is strictly monotonic,
  // any queued removal for an evicted and superseded subscription satisfies
  // `it->second->generation_ > *expected_generation` and is safely dropped as a no-op.
  if (expected_generation.has_value() && it->second->generation_ != *expected_generation) {
    ENVOY_LOG(trace,
              "Ignoring stale certificate removal for '{}' (expected gen {}, current gen {})",
              secret_name, *expected_generation, it->second->generation_);
    return;
  }
  const bool was_pending = (it->second->cert_context_ == nullptr);
  size_t notify_count = 0;
  for (const auto& pending_cb : it->second->callbacks_) {
    if (auto handle = pending_cb.handle_.lock(); handle) {
      handle->notify(nullptr);
      notify_count++;
    }
  }
  policy_.onRemoved(secret_name);
  pending_on_demand_keys_.erase(secret_name);
  disableReclaimTimerIfIdle();
  cache_.erase(it);
  disableIdleTimerIfIdle();
  updateContexts(secret_name, CachedCert{});
  stats_->cert_active_.dec();
  if (was_pending) {
    stats_->cert_pending_.dec();
  }
  ENVOY_LOG(trace, "Removed certificate subscription for '{}', notified {} pending connections",
            secret_name, notify_count);
}

HandleSharedPtr SecretManager::fetchCertificate(absl::string_view secret_name,
                                                Ssl::CertificateSelectionCallbackPtr&& cb,
                                                bool client_ocsp_capable,
                                                FairSieve::TenantId tenant_id) {
  HandleSharedPtr handle = std::make_shared<Handle>(std::move(cb), client_ocsp_capable);
  // The manager might need to be destroyed after posting from a worker because
  // the filter chain is being removed. Therefore, use a weak_ptr and ignore
  // the request to fetch a secret. Handle can also be destroyed because the
  // underlying connection is reset, and handshake is cancelled.
  factory_context_.mainThreadDispatcher().post(
      [weak_this = std::weak_ptr<SecretManager>(shared_from_this()),
       name = std::string(secret_name), weak_handle = std::weak_ptr<Handle>(handle),
       tenant_id]() mutable {
        auto that = weak_this.lock();
        auto handle = weak_handle.lock();
        if (that && handle) {
          that->addCertificateConfig(name, handle, {}, tenant_id);
        }
      });
  return handle;
}

void SecretManager::updateContexts(absl::string_view secret_name, CachedCert cached_cert,
                                   const std::optional<std::string>& evicted_name) {
  cert_contexts_.runOnAllThreads(
      [name = std::string(secret_name), cached_cert = std::move(cached_cert),
       evicted_name](OptRef<ThreadLocalCerts> certs) {
        if (evicted_name.has_value()) {
          certs->ctx_by_name_.erase(*evicted_name);
        }
        if (cached_cert.context_) {
          certs->ctx_by_name_[name] = cached_cert;
        } else {
          certs->ctx_by_name_.erase(name);
        }
      },
      [stats_scope = stats_scope_, stats = stats_] { stats->cert_updated_.inc(); });
}

std::optional<AsyncContextConstSharedPtr>
SecretManager::getContext(absl::string_view secret_name, FairSieve::TenantId tenant_id) const {
  OptRef<ThreadLocalCerts> current = cert_contexts_.get();
  if (current) {
    const auto it = current->ctx_by_name_.find(secret_name);
    if (it != current->ctx_by_name_.end()) {
      if (it->second.hit_state_ != nullptr) {
        it->second.hit_state_->recordHit(tenant_id);
      }
      return it->second.context_;
    }
  }
  return {};
}

Ssl::SelectionResult
BaseAsyncSelector::doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                      Ssl::CertificateSelectionCallbackPtr cb,
                                      absl::FunctionRef<FairSieve::TenantId()> extract_tenant) {
  std::optional<FairSieve::TenantId> cached_tenant_id;
  auto get_tenant_id = [&]() -> FairSieve::TenantId {
    if (!cached_tenant_id.has_value()) {
      cached_tenant_id = extract_tenant();
    }
    return *cached_tenant_id;
  };

  // Defer FilterState lookup + xxHash64 on cache hits when active eviction is disabled
  // (`hit_state_` is always nullptr in unbounded and pending-only modes).
  const FairSieve::TenantId hit_tenant_id =
      secret_manager_->activeLimitEnabled() ? get_tenant_id() : FairSieve::kDefaultTenantId;
  auto current_context = secret_manager_->getContext(name, hit_tenant_id);
  if (current_context) {
    ENVOY_LOG(trace, "Using an existing certificate '{}'", name);
    const Ssl::TlsContext* tls_context = &current_context.value()->tlsContext();
    const auto staple_action = ocspStapleAction(*tls_context, client_ocsp_capable,
                                                current_context.value()->ocspStaplePolicy());
    auto handle = std::make_shared<Handle>(*std::move(current_context));
    return Ssl::SelectionResult{
        .status = Ssl::SelectionResult::SelectionStatus::Success,
        .selected_ctx = tls_context,
        .staple = (staple_action == Ssl::OcspStapleAction::Staple),
        .handle = std::move(handle),
    };
  }
  ENVOY_LOG(trace, "Requesting a certificate '{}'", name);
  const FairSieve::TenantId miss_tenant_id =
      secret_manager_->trackingEnabled() ? get_tenant_id() : FairSieve::kDefaultTenantId;
  return Ssl::SelectionResult{
      .status = Ssl::SelectionResult::SelectionStatus::Pending,
      .handle = secret_manager_->fetchCertificate(name, std::move(cb), client_ocsp_capable,
                                                  miss_tenant_id),
  };
}

Ssl::SelectionResult AsyncSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                     Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromClientHello(ssl_client_hello);
  const bool client_ocsp_capable = isClientOcspCapable(ssl_client_hello);
  return doSelectTlsContext(name, client_ocsp_capable, std::move(cb), [&ssl_client_hello]() {
    return extractTenantIdFromSsl(ssl_client_hello.ssl);
  });
}

Ssl::SelectionResult UpstreamAsyncSelector::selectTlsContext(
    const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
    Ssl::CertificateSelectionCallbackPtr cb) {
  const std::string name = mapper_->deriveFromServerHello(ssl, options);
  return doSelectTlsContext(name, false, std::move(cb), [&ssl, &options]() {
    return extractTenantIdFromOptionsOrSsl(ssl, options);
  });
}

Ssl::TlsCertificateSelectorPtr
OnDemandTlsCertificateSelectorFactory::create(Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<AsyncSelector>(mapper_factory_(), secret_manager_);
}

Ssl::UpstreamTlsCertificateSelectorPtr
UpstreamOnDemandTlsCertificateSelectorFactory::createUpstreamTlsCertificateSelector(
    Ssl::TlsCertificateSelectorContext&) {
  return std::make_unique<UpstreamAsyncSelector>(mapper_factory_(), secret_manager_);
}

absl::Status BaseCertificateSelectorFactory::onConfigUpdate() {
  return secret_manager_->updateAll();
}

namespace {
template <typename MapperFactory, typename SelectorFactory>
absl::StatusOr<std::unique_ptr<SelectorFactory>>
createCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                 Server::Configuration::GenericFactoryContext& factory_context,
                                 AsyncContextFactory&& context_factory) {
  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  const uint32_t max_active = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_active_secrets, 0);
  if (max_active > 0 && !config.prefetch_secret_names().empty()) {
    absl::flat_hash_set<absl::string_view> unique_prefetches(config.prefetch_secret_names().begin(),
                                                             config.prefetch_secret_names().end());
    if (unique_prefetches.size() > max_active) {
      return absl::InvalidArgumentError(
          absl::StrCat("Number of unique prefetch_secret_names (", unique_prefetches.size(),
                       ") exceeds max_active_secrets (", max_active, ")"));
    }
  }
  if (config.has_idle_timeout() && max_active == 0) {
    return absl::InvalidArgumentError("idle_timeout requires max_active_secrets");
  }
  MapperFactory& mapper_config =
      Config::Utility::getAndCheckFactory<MapperFactory>(config.certificate_mapper());
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      config.certificate_mapper().typed_config(), factory_context.messageValidationVisitor(),
      mapper_config);
  auto mapper_factory = mapper_config.createTlsCertificateMapperFactory(*message, factory_context);
  RETURN_IF_NOT_OK(mapper_factory.status());
  // Doing this last since it can kick-start SDS fetches.
  // Envoy ensures that per-worker TLS sockets are destroyed before the filter
  // chain holding the TLS socket factory using a completion. This means the
  // TLS context config in the lambda will outlive each AsyncSelector, and it
  // is safe to refer to TLS context config by reference.
  auto secret_manager =
      std::make_shared<SecretManager>(config, factory_context, std::move(context_factory));
  return std::make_unique<SelectorFactory>(*std::move(mapper_factory), std::move(secret_manager));
}
} // namespace

absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
OnDemandTlsCertificateSelectorConfigFactory::createTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ServerContextConfig& tls_config, bool for_quic) {
  if (for_quic) {
    return absl::InvalidArgumentError("Does not support QUIC listeners.");
  }
  // Session ID is currently generated from server names and the included TLS
  // certificates in the parent TLS context. It would not be safe to allow
  // resuming with this ID for on-demand TLS certificates which are not present
  // in the parent TLS context.
  if (!tls_config.disableStatelessSessionResumption() ||
      !tls_config.disableStatefulSessionResumption()) {
    return absl::InvalidArgumentError(
        "On demand certificates are not integrated with session resumption support.");
  }
  return createCertificateSelectorFactory<Ssl::TlsCertificateMapperConfigFactory,
                                          OnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ServerAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
UpstreamOnDemandTlsCertificateSelectorConfigFactory::createUpstreamTlsCertificateSelectorFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ClientContextConfig& tls_config) {
  return createCertificateSelectorFactory<Ssl::UpstreamTlsCertificateMapperConfigFactory,
                                          UpstreamOnDemandTlsCertificateSelectorFactory>(
      proto_config, factory_context,
      [&tls_config](Stats::Scope& scope,
                    Server::Configuration::ServerFactoryContext& server_factory_context,
                    const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status) {
        return std::make_shared<ClientAsyncContext>(scope, server_factory_context, tls_config,
                                                    cert_config, creation_status);
      });
}

REGISTER_FACTORY(OnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::TlsCertificateSelectorConfigFactory);

REGISTER_FACTORY(UpstreamOnDemandTlsCertificateSelectorConfigFactory,
                 Ssl::UpstreamTlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
