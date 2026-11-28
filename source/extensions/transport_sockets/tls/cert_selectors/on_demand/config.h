#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/fair_sieve_cache.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/common/async_context.h"

#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

// Well-known FilterState key (Router::StringAccessor) carrying the tenant identifier used for
// per-tenant fair sharing in the on-demand secret cache.
inline constexpr absl::string_view TenantIdFilterStateKey =
    "envoy.tls.certificate_selectors.on_demand_secret.tenant_id";

#define ALL_CERT_SELECTION_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(cert_requested)                                                                          \
  COUNTER(cert_updated)                                                                            \
  COUNTER(cert_evicted)                                                                            \
  COUNTER(cert_idle_expired)                                                                       \
  COUNTER(cert_ownership_transferred)                                                              \
  COUNTER(cert_pending_rejected)                                                                   \
  GAUGE(cert_active, Accumulate)                                                                   \
  GAUGE(cert_pending, Accumulate)

struct CertSelectionStats {
  ALL_CERT_SELECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)
};
using CertSelectionStatsSharedPtr = std::shared_ptr<CertSelectionStats>;

using ConfigProto =
    envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config;
using UpdateCb = std::function<absl::Status(absl::string_view, const Ssl::TlsCertificateConfig&)>;
using RemoveCb = std::function<absl::Status(absl::string_view)>;

class AsyncContextConfig : public Event::DeferredDeletable {
public:
  AsyncContextConfig(absl::string_view cert_name,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const envoy::config::core::v3::ConfigSource& config_source,
                     OptRef<Init::Manager> init_manager, UpdateCb update_cb, RemoveCb remove_cb);
  const std::optional<Ssl::TlsCertificateConfigImpl>& certConfig() const { return cert_config_; }

  // Event::DeferredDeletable
  void deleteIsPending() override {
    // Unregister callbacks immediately so no further update/remove callbacks from the same xDS
    // batch can fire for an evicted secret before deferred deletion completes.
    update_cb_handle_.reset();
    remove_cb_handle_.reset();
  }

private:
  absl::Status loadCert();

  Server::Configuration::ServerFactoryContext& factory_context_;
  const std::string cert_name_;
  std::optional<Ssl::TlsCertificateConfigImpl> cert_config_;
  const Secret::TlsCertificateConfigProviderSharedPtr cert_provider_;
  UpdateCb update_cb_;
  Common::CallbackHandlePtr update_cb_handle_;
  RemoveCb remove_cb_;
  Common::CallbackHandlePtr remove_cb_handle_;
};
using AsyncContextConfigConstPtr = std::unique_ptr<AsyncContextConfig>;

class Handle : public Ssl::SelectionHandle {
public:
  /**
   * Synchronous handle constructor must extend the context lifetime since it holds the low TLS
   * context.
   */
  explicit Handle(AsyncContextConstSharedPtr cert_context) : active_context_(cert_context) {}

  /**
   * Asynchronous handle constructor must also keep the callback for the secret manager.
   */
  Handle(Ssl::CertificateSelectionCallbackPtr&& cb, bool client_ocsp_capable)
      : cb_(std::move(cb)), client_ocsp_capable_(client_ocsp_capable) {}

  /**
   * Notify the pending connection that the context is available.
   * @param cert_ctx TLS context or nullptr is the secret is removed.
   */
  void notify(AsyncContextConstSharedPtr cert_ctx);

private:
  // Captures the selected certificate data for the duration of the socket.
  AsyncContextConstSharedPtr active_context_;
  Ssl::CertificateSelectionCallbackPtr cb_;
  bool client_ocsp_capable_{false};
};

using HandleSharedPtr = std::shared_ptr<Handle>;

/**
 * Secret manager maintains dynamic subscriptions to SDS secrets and converts them from the xDS form
 * to the boringssl TLS contexts, while applying the parent TLS configuration.
 */
class SecretManager : public std::enable_shared_from_this<SecretManager>,
                      protected Logger::Loggable<Logger::Id::secret> {
public:
  SecretManager(const ConfigProto& config,
                Server::Configuration::GenericFactoryContext& factory_context,
                AsyncContextFactory&& context_factory);
  ~SecretManager();

  /**
   * Start a subscription to the secret and register a handle on updates.
   * MUST be called on the main thread.
   */
  void addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                            OptRef<Init::Manager> init_manager,
                            FairSieve::TenantId tenant_id = FairSieve::kDefaultTenantId);
  /**
   * Handle an updated certificate config by notifying any pending connections.
   * MUST be called on the main thread.
   */
  absl::Status updateCertificate(absl::string_view secret_name,
                                 const Ssl::TlsCertificateConfig& cert_config);

  /**
   * Recreates all contexts due to an update to the context config.
   * MUST be called on the main thread.
   */
  absl::Status updateAll();

  /**
   * Delete any cached state for the secret including its active subscription, gated by
   * `expected_generation` when supplied so an older queued removal cannot erase a newer entry
   * created for the same secret name after eviction and re-fetch (ABA safety).
   */
  absl::Status removeCertificateConfig(absl::string_view secret_name,
                                       std::optional<uint64_t> expected_generation = std::nullopt);

  /**
   * Retrieve the thread local certificate and record a hit for `tenant_id` when bounded eviction
   * is enabled.
   */
  std::optional<AsyncContextConstSharedPtr>
  getContext(absl::string_view secret_name,
             FairSieve::TenantId tenant_id = FairSieve::kDefaultTenantId) const;

  /**
   * Start fetching the certificate via a subscription.
   */
  HandleSharedPtr fetchCertificate(absl::string_view secret_name,
                                   Ssl::CertificateSelectionCallbackPtr&& cb,
                                   bool client_ocsp_capable,
                                   FairSieve::TenantId tenant_id = FairSieve::kDefaultTenantId);

  /**
   * Returns true if either `max_active_secrets` or `max_pending_secrets` is configured.
   */
  bool trackingEnabled() const { return policy_.trackingEnabled(); }

  /**
   * Minimum age an abandoned (all waiting handles disconnected) non-prefetched pending entry must
   * reach before `reclaimAbandonedPending()` may reclaim its slot. Prevents fast-disconnecting
   * clients from bypassing `max_pending_secrets` and amplifying SDS/signing load while still
   * recovering cleanly from wedged SDS subscriptions after client handshake timeouts.
   */
  static constexpr std::chrono::seconds kMinAbandonedPendingAge{5};

  /**
   * Number of idle sweeps per configured `idle_timeout`. A loaded non-prefetched secret expires
   * once no handshake selects it during this many consecutive sweep intervals, i.e., between 1x
   * and `(1 + 1 / kIdleSweepsPerTimeout)x` `idle_timeout` after its last use.
   */
  static constexpr uint32_t kIdleSweepsPerTimeout{4};

  /**
   * Maximum number of idle secrets inspected and removed per main-thread event-loop tick during
   * an idle sweep. Larger sweeps yield via a 0 ms timer so post-flood expiration batches do not
   * stall handshakes, SDS callbacks, or admin probes on the main thread.
   */
  static constexpr size_t kMaxIdleRemovalsPerTick{256};

  /**
   * Returns true if `max_active_secrets` is configured (so hits update `HitState`).
   */
  bool activeLimitEnabled() const { return policy_.activeLimitEnabled(); }

private:
  struct CachedCert {
    AsyncContextConstSharedPtr context_;
    FairSieve::HitStateSharedPtr hit_state_;
  };

  struct PendingCallback {
    std::weak_ptr<Handle> handle_;
    FairSieve::TenantId tenant_id_{FairSieve::kDefaultTenantId};
    bool is_coalesced_{false};
  };

  void updateContexts(absl::string_view secret_name, CachedCert cached_cert,
                      const std::optional<std::string>& evicted_name = std::nullopt);
  void doRemoveCertificateConfig(absl::string_view secret_name,
                                 std::optional<uint64_t> expected_generation);
  // Periodically reclaims abandoned non-prefetched pending entries (all waiting handles
  // disconnected and age >= `kMinAbandonedPendingAge`) off the admission hot path, re-arming
  // `reclaim_timer_` only while `pending_on_demand_keys_` remains non-empty.
  void reclaimAbandonedPending();
  void disableReclaimTimerIfIdle();
  // Runs or continues a paced idle sweep, removing at most `kMaxIdleRemovalsPerTick` idle
  // non-prefetched secrets per tick and yielding via a 0 ms timer while a sweep remains in
  // progress. Re-arms at `idle_sweep_interval_` once complete if any non-prefetched active secret
  // remains.
  void sweepIdleSecrets();
  bool hasActiveNonPrefetchedSecret() const;
  void disableIdleTimerIfIdle();
  // Drops `secret_names` from every worker's lookup map with one cross-thread broadcast.
  void removeContexts(std::vector<std::string>&& secret_names);
  const Stats::ScopeSharedPtr stats_scope_;
  // Gauge conservation invariants (main thread, outside active mutations):
  //  - `cert_active == cache_.size() == n_ready + n_pending`
  //  - `cert_pending == n_pending`
  //  - `cert_requested == cert_active + cert_evicted + cert_idle_expired + n_removed`
  CertSelectionStatsSharedPtr stats_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  AsyncContextFactory context_factory_;
  FairSieve::Policy policy_;
  // Strictly monotonic allocation counter used to tag each `CacheEntry` for ABA-safe async
  // removal (`live_entry.generation_ > stale_posted_generation` after eviction + re-fetch).
  uint64_t next_entry_generation_{1};

  // Main-thread accessible context config subscriptions and callbacks. Stored via `unique_ptr` for
  // pointer stability across synchronous eviction inside `AsyncContextConfig` construction (and a
  // newly inserted pending entry can never self-evict during its own activation).
  struct CacheEntry {
    uint64_t generation_{0};
    FairSieve::TenantId owner_tenant_{FairSieve::kDefaultTenantId};
    MonotonicTime created_at_{};
    AsyncContextConfigConstPtr cert_config_;
    AsyncContextConstSharedPtr cert_context_;
    FairSieve::HitStateSharedPtr hit_state_;
    std::vector<PendingCallback> callbacks_;
  };
  absl::flat_hash_map<std::string, std::unique_ptr<CacheEntry>> cache_;

  // Tracks in-flight non-prefetched pending secret names when `max_pending_secrets` is enabled so
  // `reclaim_timer_` can sweep abandoned pending subscriptions in O(max_pending) without scanning
  // active entries or adding per-rejection overhead.
  absl::flat_hash_set<std::string> pending_on_demand_keys_;

  // Lock-free map to retrieve ready TLS contexts by name.
  struct ThreadLocalCerts : public ThreadLocal::ThreadLocalObject {
    absl::flat_hash_map<std::string, CachedCert> ctx_by_name_;
  };
  ThreadLocal::TypedSlot<ThreadLocalCerts> cert_contexts_;

  // Idle-disarmed main-thread timer (declared last so it is destroyed first) that periodically
  // invokes `reclaimAbandonedPending()` while `pending_on_demand_keys_` is non-empty.
  Event::TimerPtr reclaim_timer_;

  struct PendingIdleRemoval {
    std::string name_;
    uint64_t generation_{0};
  };

  // Prefetched secret names exempt from `idle_timeout` expiration (populated only when
  // `idle_timeout` is enabled).
  absl::flat_hash_set<std::string> prefetched_names_;
  // ABA-tagged secrets queued for paced removal across event-loop ticks during an active sweep.
  std::vector<PendingIdleRemoval> pending_idle_removals_;
  // `idle_timeout / kIdleSweepsPerTimeout`, and the idle-disarmed main-thread timer driving
  // `sweepIdleSecrets()` while any non-prefetched active secret is loaded. Null unless both
  // `idle_timeout` and `max_active_secrets` are configured.
  std::chrono::milliseconds idle_sweep_interval_{0};
  Event::TimerPtr idle_timer_;
};

class BaseAsyncSelector : protected Logger::Loggable<Logger::Id::connection> {
public:
  BaseAsyncSelector(std::shared_ptr<SecretManager>& secret_manager)
      : secret_manager_(secret_manager) {}

protected:
  Ssl::SelectionResult doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                          Ssl::CertificateSelectionCallbackPtr cb,
                                          absl::FunctionRef<FairSieve::TenantId()> extract_tenant);
  std::shared_ptr<SecretManager> secret_manager_;
};

/**
 * An asynchronous certificate selector is created for each TLS socket on each worker.
 */
class AsyncSelector : public BaseAsyncSelector, public Ssl::TlsCertificateSelector {
public:
  AsyncSelector(Ssl::TlsCertificateMapperPtr&& mapper,
                std::shared_ptr<SecretManager>& secret_manager)
      : BaseAsyncSelector(secret_manager), mapper_(std::move(mapper)) {}

  // Ssl::TlsCertificateSelector
  bool providesCertificates() const override { return true; }
  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("Not supported with QUIC");
  };

private:
  Ssl::TlsCertificateMapperPtr mapper_;
};

class UpstreamAsyncSelector : public BaseAsyncSelector, public Ssl::UpstreamTlsCertificateSelector {
public:
  UpstreamAsyncSelector(Ssl::UpstreamTlsCertificateMapperPtr&& mapper,
                        std::shared_ptr<SecretManager>& secret_manager)
      : BaseAsyncSelector(secret_manager), mapper_(std::move(mapper)) {}
  // Ssl::UpstreamTlsCertificateSelector
  Ssl::SelectionResult
  selectTlsContext(const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
                   Ssl::CertificateSelectionCallbackPtr cb) override;

private:
  Ssl::UpstreamTlsCertificateMapperPtr mapper_;
};

class BaseCertificateSelectorFactory {
public:
  BaseCertificateSelectorFactory(std::shared_ptr<SecretManager>&& secret_manager)
      : secret_manager_(std::move(secret_manager)) {}
  absl::Status onConfigUpdate();

protected:
  std::shared_ptr<SecretManager> secret_manager_;
};

class OnDemandTlsCertificateSelectorFactory : public BaseCertificateSelectorFactory,
                                              public Ssl::TlsCertificateSelectorFactory {
public:
  OnDemandTlsCertificateSelectorFactory(Ssl::TlsCertificateMapperFactory&& mapper_factory,
                                        std::shared_ptr<SecretManager>&& secret_manager)
      : BaseCertificateSelectorFactory(std::move(secret_manager)),
        mapper_factory_(std::move(mapper_factory)) {}
  // Ssl::TlsCertificateSelectorFactory
  Ssl::TlsCertificateSelectorPtr create(Ssl::TlsCertificateSelectorContext&) override;
  absl::Status onConfigUpdate() override {
    return BaseCertificateSelectorFactory::onConfigUpdate();
  }

private:
  Ssl::TlsCertificateMapperFactory mapper_factory_;
};

class UpstreamOnDemandTlsCertificateSelectorFactory
    : public BaseCertificateSelectorFactory,
      public Ssl::UpstreamTlsCertificateSelectorFactory {
public:
  UpstreamOnDemandTlsCertificateSelectorFactory(
      Ssl::UpstreamTlsCertificateMapperFactory&& mapper_factory,
      std::shared_ptr<SecretManager>&& secret_manager)
      : BaseCertificateSelectorFactory(std::move(secret_manager)),
        mapper_factory_(std::move(mapper_factory)) {}
  // Ssl::UpstreamTlsCertificateSelectorFactory
  Ssl::UpstreamTlsCertificateSelectorPtr
  createUpstreamTlsCertificateSelector(Ssl::TlsCertificateSelectorContext&) override;
  absl::Status onConfigUpdate() override {
    return BaseCertificateSelectorFactory::onConfigUpdate();
  }

private:
  Ssl::UpstreamTlsCertificateMapperFactory mapper_factory_;
};

class OnDemandTlsCertificateSelectorConfigFactory
    : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig& tls_config,
                                      bool for_quic) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_selectors.on_demand_secret"; }
};

DECLARE_FACTORY(OnDemandTlsCertificateSelectorConfigFactory);

class UpstreamOnDemandTlsCertificateSelectorConfigFactory
    : public Ssl::UpstreamTlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::UpstreamTlsCertificateSelectorFactoryPtr>
  createUpstreamTlsCertificateSelectorFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context,
      const Ssl::ClientContextConfig& tls_config) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_selectors.on_demand_secret"; }
};

DECLARE_FACTORY(UpstreamOnDemandTlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
