#pragma once

#include <atomic>

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/server_context_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {

#define ALL_CERT_SELECTION_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(cert_requested)                                                                          \
  COUNTER(cert_updated)                                                                            \
  COUNTER(cert_overflow)                                                                           \
  COUNTER(cert_evicted)                                                                            \
  COUNTER(cert_reclaimed)                                                                          \
  GAUGE(cert_active, Accumulate)

struct CertSelectionStats {
  ALL_CERT_SELECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)
};
using CertSelectionStatsSharedPtr = std::shared_ptr<CertSelectionStats>;

class AsyncContext;
using AsyncContextConstSharedPtr = std::shared_ptr<const AsyncContext>;
using AsyncContextFactory = absl::AnyInvocable<AsyncContextConstSharedPtr(
    Stats::Scope&, Server::Configuration::ServerFactoryContext&, const Ssl::TlsCertificateConfig&,
    absl::Status&)>;

using ConfigProto =
    envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config;
using UpdateCb = std::function<absl::Status(absl::string_view, const Ssl::TlsCertificateConfig&)>;
using RemoveCb = std::function<absl::Status(absl::string_view)>;

class AsyncContextConfig {
public:
  AsyncContextConfig(absl::string_view cert_name,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const envoy::config::core::v3::ConfigSource& config_source,
                     OptRef<Init::Manager> init_manager, UpdateCb update_cb, RemoveCb remove_cb);
  const std::optional<Ssl::TlsCertificateConfigImpl>& certConfig() const { return cert_config_; }

private:
  absl::Status loadCert();

  Server::Configuration::ServerFactoryContext& factory_context_;
  const std::string cert_name_;
  std::optional<Ssl::TlsCertificateConfigImpl> cert_config_;
  const Secret::TlsCertificateConfigProviderSharedPtr cert_provider_;
  UpdateCb update_cb_;
  const Common::CallbackHandlePtr update_cb_handle_;
  RemoveCb remove_cb_;
  const Common::CallbackHandlePtr remove_cb_handle_;
};
using AsyncContextConfigConstPtr = std::unique_ptr<AsyncContextConfig>;

class AsyncContext {
public:
  virtual ~AsyncContext() = default;

  explicit AsyncContext(Stats::Scope& scope) : scope_(scope.createScope("")) {}

  /**
   * @return OCSP policy for the certificate, only needed by the server TLS context.
   */
  virtual Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const PURE;

  /**
   * @return the low-level TLS context stored in this context.
   */
  virtual const Ssl::TlsContext& tlsContext() const PURE;

  /**
   * @return certificate-specific stats scope.
   */
  Stats::Scope& certScope() const { return *scope_; }

  /**
   * Mark the context as used by a handshake. May be called from any thread. Checks before
   * writing so that the hot path does not repeatedly dirty a cache line shared by all workers.
   */
  void markUsed() const {
    if (!used_->load(std::memory_order_relaxed)) {
      used_->store(true, std::memory_order_relaxed);
    }
  }

  /**
   * @return whether the context was used by a handshake since the last call, clearing the flag.
   */
  bool consumeUsed() const { return used_->exchange(false, std::memory_order_relaxed); }

  /**
   * Share the handshake-use flag with the context this one replaces. Workers may keep serving
   * the prior context until the thread local update propagates; sharing the flag object ensures
   * their late use marks are not lost across certificate updates. Must be called on the main
   * thread before the context is published.
   */
  void adoptUsedFlag(const AsyncContext& prior) const { used_ = prior.used_; }

private:
  Stats::ScopeSharedPtr scope_;
  // Tracks handshake usage for idle eviction, shared between the contexts that replace each
  // other for one secret name. Starts unused: the grace period for a new cache entry is granted
  // by CacheEntry::recently_active_ instead, so that there is a single idle period of slack
  // regardless of how often the certificate is updated.
  mutable std::shared_ptr<std::atomic<bool>> used_{std::make_shared<std::atomic<bool>>(false)};
};

class ServerAsyncContext : public AsyncContext,
                           public Extensions::TransportSockets::Tls::ServerContextImpl {
public:
  ServerAsyncContext(Stats::Scope& scope,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const Ssl::ServerContextConfig& tls_config,
                     const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status)
      : AsyncContext(scope),
        ServerContextImpl(
            certScope(), tls_config,
            std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>{cert_config},
            false, factory_context, /** used by quic */ nullptr, creation_status) {}

  Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const override {
    return ocsp_staple_policy_;
  }
  const Ssl::TlsContext& tlsContext() const override;
};

class ClientAsyncContext : public AsyncContext,
                           public Extensions::TransportSockets::Tls::ClientContextImpl {
public:
  ClientAsyncContext(Stats::Scope& scope,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const Ssl::ClientContextConfig& tls_config,
                     const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status)
      : AsyncContext(scope),
        ClientContextImpl(
            certScope(), tls_config,
            std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>{cert_config},
            false, factory_context, creation_status) {}

  Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const override {
    // This is unused but we must return something.
    return Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling;
  }

  const Ssl::TlsContext& tlsContext() const override;
};

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

  /**
   * Start a subscription to the secret and register a handle on updates.
   * MUST be called on the main thread.
   */
  void addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                            OptRef<Init::Manager> init_manager);
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
   * Delete any cached state for the secret including its active subscription.
   */
  absl::Status removeCertificateConfig(absl::string_view);

  /**
   * Update the thread local caches with the certificates.
   * @param cert_ctx the new context or nullptr to remove the secret.
   */
  void setContext(absl::string_view secret_name, AsyncContextConstSharedPtr cert_ctx);

  /**
   * Retrieve the thread local certificate.
   */
  std::optional<AsyncContextConstSharedPtr> getContext(absl::string_view secret_name) const;

  /**
   * Start fetching the certificate via a subscription.
   */
  HandleSharedPtr fetchCertificate(absl::string_view secret_name,
                                   Ssl::CertificateSelectionCallbackPtr&& cb,
                                   bool client_ocsp_capable);

  /**
   * @return the number of pending handshake callbacks tracked for the secret. Test use only.
   */
  size_t pendingCallbacksForTest(absl::string_view secret_name) const;

private:
  void doRemoveCertificateConfig(absl::string_view,
                                 std::optional<uint64_t> expected_generation = {});
  void evictIdle();
  bool reclaimUnusedEntry();
  const Stats::ScopeSharedPtr stats_scope_;
  CertSelectionStatsSharedPtr stats_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  // The maximum number of cache entries, with 0 meaning unlimited.
  const uint32_t max_secrets_;
  // The idle duration after which an unused cache entry is evicted, if configured.
  const std::optional<std::chrono::milliseconds> idle_timeout_;
  // Configured prefetch names remain pinned in the cache even if the secret is removed by the
  // SDS server and later fetched again on-demand.
  const absl::flat_hash_set<std::string> prefetch_names_;
  AsyncContextFactory context_factory_;
  Event::TimerPtr idle_timer_;

  // Main-thread accessible context config subscriptions and callbacks.
  struct CacheEntry {
    AsyncContextConfigConstPtr cert_config_;
    AsyncContextConstSharedPtr cert_context_;
    std::vector<std::weak_ptr<Handle>> callbacks_;
    // Distinguishes entry incarnations for the same secret name, so that a deferred removal does
    // not erase an entry that was reclaimed and re-created while the removal was in flight.
    uint64_t generation_{0};
    // Expired handles are compacted when the callback list reaches this size, which then doubles,
    // keeping the insertion cost amortized constant and the list bounded by roughly twice the
    // live pending handshakes.
    size_t next_compact_size_{16};
    // Prefetched secrets are pinned in the cache: they are never idle-evicted or reclaimed.
    bool prefetched_{false};
    // Set on main-thread handshake activity (entry creation, fetch attach) and cleared by each
    // idle sweep, guaranteeing an entry survives at least one full idle period after activity.
    // SDS updates deliberately do not set this: an updated but never used secret is still idle.
    bool recently_active_{true};
  };
  absl::flat_hash_map<std::string, CacheEntry> cache_;
  uint64_t next_generation_{0};

  // Lock-free map to retrieve ready TLS contexts by name.
  struct ThreadLocalCerts : public ThreadLocal::ThreadLocalObject {
    absl::flat_hash_map<std::string, AsyncContextConstSharedPtr> ctx_by_name_;
  };
  ThreadLocal::TypedSlot<ThreadLocalCerts> cert_contexts_;
};

class BaseAsyncSelector : protected Logger::Loggable<Logger::Id::connection> {
public:
  BaseAsyncSelector(std::shared_ptr<SecretManager>& secret_manager)
      : secret_manager_(secret_manager) {}

protected:
  Ssl::SelectionResult doSelectTlsContext(const std::string& name, const bool client_ocsp_capable,
                                          Ssl::CertificateSelectionCallbackPtr cb);
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
