#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"
#include "source/extensions/transport_sockets/tls/cert_selectors/common/async_context.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModules {

// Function pointer types for the TLS certificate selector ABI hooks.
using OnTlsCertificateSelectorConfigNewType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_config_new);
using OnTlsCertificateSelectorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_config_destroy);
using OnTlsCertificateSelectorNewType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_new);
using OnTlsCertificateSelectorDestroyType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_destroy);
using OnTlsCertificateSelectorSelectType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_select);
using OnTlsCertificateSelectorConfigCertificateRequestedType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_config_certificate_requested);
using OnTlsCertificateSelectorConfigScheduledType =
    decltype(&envoy_dynamic_module_on_tls_certificate_selector_config_scheduled);

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

/**
 * Selection handle of a dynamic certificate. It keeps the certificate alive for the handshake that
 * uses it, or holds the handshake that waits for it.
 */
class DynamicCertificateHandle : public Ssl::SelectionHandle {
public:
  explicit DynamicCertificateHandle(AsyncContextConstSharedPtr context)
      : context_(std::move(context)) {}
  DynamicCertificateHandle(Ssl::CertificateSelectionCallbackPtr&& cb, bool client_ocsp_capable)
      : cb_(std::move(cb)), client_ocsp_capable_(client_ocsp_capable) {}

  /**
   * Resumes the waiting handshake on the dispatcher of its connection.
   * @param context the dynamic certificate to use, or nullptr to fail the handshake.
   */
  void notify(AsyncContextConstSharedPtr context);

private:
  AsyncContextConstSharedPtr context_;
  Ssl::CertificateSelectionCallbackPtr cb_;
  const bool client_ocsp_capable_{false};
};

/**
 * Configuration holding the loaded dynamic module, the resolved ABI function pointers, and the
 * in-module configuration. This is shared between the factory and every selector it creates, which
 * keeps the module loaded for as long as any selector is alive. It also holds the module metrics
 * and the dynamic certificates.
 */
class DynamicModuleTlsCertificateSelectorConfig
    : public std::enable_shared_from_this<DynamicModuleTlsCertificateSelectorConfig>,
      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * @param dynamic_module the loaded dynamic module.
   * @param factory_context the factory context of the TLS context.
   * @param tls_config the TLS context configuration, which the selector factory detaches when it is
   * destroyed.
   * @param metrics_namespace the namespace of the module metrics.
   * @param dynamic_context_factory builds the TLS context of a dynamic certificate, or is empty
   * when dynamic certificates are not allowed.
   */
  DynamicModuleTlsCertificateSelectorConfig(
      Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
      Server::Configuration::GenericFactoryContext& factory_context,
      const Ssl::ContextConfig& tls_config, absl::string_view metrics_namespace,
      AsyncContextFactory dynamic_context_factory);
  ~DynamicModuleTlsCertificateSelectorConfig();

  /**
   * Provides or replaces a dynamic certificate. Must be called on the main thread.
   * @return true if the certificate is set.
   */
  bool setCertificate(absl::string_view name, absl::string_view certificate_chain,
                      absl::string_view private_key, absl::string_view ocsp_staple);

  /**
   * Removes a dynamic certificate and fails the handshakes that wait for it. Must be called on the
   * main thread.
   */
  void removeCertificate(absl::string_view name);

  /**
   * Rebuilds the dynamic certificates with the updated TLS context configuration, and removes the
   * ones that fail with it. Must be called on the main thread.
   */
  void onConfigUpdate();

  /**
   * Stops using the TLS context configuration, which is destroyed with the selector factory. Must
   * be called on the main thread.
   */
  void detachTlsConfig();

  /**
   * Serves a dynamic certificate on the thread running the handshake, or holds the handshake until
   * the module provides or removes it.
   */
  Ssl::SelectionResult selectDynamicCertificate(absl::string_view name, bool client_ocsp_capable,
                                                Ssl::CertificateSelectionCallbackPtr&& cb);

  bool allowsDynamicCertificates() const { return thread_local_certificates_ != nullptr; }

  /**
   * Invokes the module's scheduled hook if present. Called on the main thread by the scheduler.
   */
  void onScheduled(uint64_t event_id);

  Envoy::Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr in_module_config_ = nullptr;

  // Required function pointers. All are guaranteed non-null after successful creation.
  OnTlsCertificateSelectorConfigDestroyType on_config_destroy_ = nullptr;
  OnTlsCertificateSelectorNewType on_new_ = nullptr;
  OnTlsCertificateSelectorDestroyType on_destroy_ = nullptr;
  OnTlsCertificateSelectorSelectType on_select_ = nullptr;

  // Optional function pointers. Set only when the module implements the hook.
  OnTlsCertificateSelectorConfigCertificateRequestedType on_certificate_requested_ = nullptr;
  OnTlsCertificateSelectorConfigScheduledType on_config_scheduled_ = nullptr;

  // The main thread dispatcher used to post work back to the main thread.
  Event::Dispatcher& main_thread_dispatcher_;

  // Metrics can only be defined during config_new, so the registry needs no lock. The flag is
  // stored with release ordering once the configuration is created and loaded with acquire ordering
  // when the module defines a metric.
  std::atomic<bool> stat_creation_frozen_{false};

private:
  friend class DynamicModuleTlsCertificateSelectorConfigPeer;

  struct DynamicCertificate {
    // Set once the module provides the certificate.
    std::optional<Ssl::TlsCertificateConfigImpl> certificate_config;
    // Set once the certificate is built, which waits for the validation context of the TLS context.
    AsyncContextConstSharedPtr context;
    std::vector<std::weak_ptr<DynamicCertificateHandle>> waiting_handles;
  };

  struct ThreadLocalCertificates : public ThreadLocal::ThreadLocalObject {
    absl::flat_hash_map<std::string, AsyncContextConstSharedPtr> contexts;
  };

  absl::Status addCertificate(const std::string& name, absl::string_view certificate_chain,
                              absl::string_view private_key, absl::string_view ocsp_staple);
  // Fails the handshakes that wait for a requested certificate that the module could not provide.
  void failRequestedCertificate(absl::string_view name);
  void requestCertificate(const std::string& name,
                          const std::weak_ptr<DynamicCertificateHandle>& weak_handle);
  absl::StatusOr<AsyncContextConstSharedPtr>
  buildContext(const Ssl::TlsCertificateConfig& certificate_config);
  void activateCertificate(const std::string& name, DynamicCertificate& certificate,
                           AsyncContextConstSharedPtr context);
  void updateWorkers(const std::string& name, AsyncContextConstSharedPtr context);

  const Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  Server::Configuration::ServerFactoryContext& server_context_;
  // Null once the selector factory is destroyed.
  const Ssl::ContextConfig* tls_config_;
  AsyncContextFactory dynamic_context_factory_;
  // Scope of the dynamic certificate TLS contexts, so their stats aggregate with the stats of the
  // TLS context.
  const Stats::ScopeSharedPtr context_scope_;
  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  Envoy::Extensions::DynamicModules::MetricRegistry metrics_;
  // The dynamic certificates by name. Only accessed on the main thread.
  absl::flat_hash_map<std::string, DynamicCertificate> dynamic_certificates_;
  // Lock-free lookups of the built dynamic certificates on the worker threads. Allocated only when
  // dynamic certificates are allowed.
  ThreadLocal::TypedSlotPtr<ThreadLocalCertificates> thread_local_certificates_;
};

using DynamicModuleTlsCertificateSelectorConfigSharedPtr =
    std::shared_ptr<DynamicModuleTlsCertificateSelectorConfig>;

/**
 * Creates a new DynamicModuleTlsCertificateSelectorConfig. Resolves the ABI symbols and creates the
 * in-module config. Returns an error if symbol resolution or module initialization fails.
 */
absl::StatusOr<DynamicModuleTlsCertificateSelectorConfigSharedPtr>
newDynamicModuleTlsCertificateSelectorConfig(
    absl::string_view selector_name, absl::string_view selector_config, bool is_upstream,
    absl::string_view metrics_namespace,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Server::Configuration::GenericFactoryContext& factory_context,
    const Ssl::ContextConfig& tls_config, AsyncContextFactory dynamic_context_factory);

/**
 * Scheduler that dispatches events to a certificate selector configuration on the main thread. The
 * module can hold it on any thread and call commit() to run the scheduled hook on the main thread.
 */
class DynamicModuleTlsCertificateSelectorConfigScheduler {
public:
  explicit DynamicModuleTlsCertificateSelectorConfigScheduler(
      std::weak_ptr<DynamicModuleTlsCertificateSelectorConfig> config)
      : config_(std::move(config)) {}

  void commit(uint64_t event_id);

private:
  std::weak_ptr<DynamicModuleTlsCertificateSelectorConfig> config_;
};

/**
 * The Envoy side of a single certificate selection, which the module receives as the selection
 * context.
 */
struct TlsCertificateSelectionContext {
  const SSL& ssl;
  // The ClientHello of a downstream TLS context, or nullptr for an upstream TLS context.
  const SSL_CLIENT_HELLO* client_hello;
};

/**
 * Certificate selector backed by a dynamic module. One is created for each downstream or upstream
 * TLS context, and it is shared by every handshake that uses the TLS context.
 */
class DynamicModuleTlsCertificateSelector : public Ssl::TlsCertificateSelector,
                                            public Ssl::UpstreamTlsCertificateSelector,
                                            public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * @param default_selector serves the Default outcome of a downstream TLS context, or is nullptr
   * for an upstream TLS context.
   */
  DynamicModuleTlsCertificateSelector(
      DynamicModuleTlsCertificateSelectorConfigSharedPtr config,
      Ssl::TlsCertificateSelectorContext& selector_context,
      Ssl::TlsCertificateSelectorPtr default_selector,
      Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy);
  ~DynamicModuleTlsCertificateSelector() override;

  // Ssl::TlsCertificateSelector
  bool providesCertificates() const override { return config_->allowsDynamicCertificates(); }
  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;
  // The config factory rejects QUIC listeners, which are the only callers.
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("not implemented");
  }

  // Ssl::UpstreamTlsCertificateSelector
  Ssl::SelectionResult
  selectTlsContext(const SSL& ssl, const Network::TransportSocketOptionsConstSharedPtr& options,
                   Ssl::CertificateSelectionCallbackPtr cb) override;

private:
  Ssl::SelectionResult selectCertificate(TlsCertificateSelectionContext& context,
                                         bool client_ocsp_capable,
                                         Ssl::CertificateSelectionCallbackPtr&& cb);

  const DynamicModuleTlsCertificateSelectorConfigSharedPtr config_;
  const std::vector<Ssl::TlsContext>& tls_contexts_;
  const Ssl::TlsCertificateSelectorPtr default_selector_;
  const Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy_;
  envoy_dynamic_module_type_tls_certificate_selector_module_ptr in_module_selector_ = nullptr;
};

/**
 * Factory that creates a dynamic module certificate selector for each downstream TLS context.
 */
class DynamicModuleTlsCertificateSelectorFactory : public Ssl::TlsCertificateSelectorFactory {
public:
  // The TLS context configuration owns this factory, so it outlives the factory.
  DynamicModuleTlsCertificateSelectorFactory(
      DynamicModuleTlsCertificateSelectorConfigSharedPtr config,
      const Ssl::ServerContextConfig& server_config)
      : config_(std::move(config)), server_config_(server_config) {}
  ~DynamicModuleTlsCertificateSelectorFactory() override { config_->detachTlsConfig(); }

  // Ssl::TlsCertificateSelectorFactory
  Ssl::TlsCertificateSelectorPtr
  create(Ssl::TlsCertificateSelectorContext& selector_context) override;
  // An SDS update creates a new TLS context, whose selector receives the new certificates, so only
  // the dynamic certificates are rebuilt here.
  absl::Status onConfigUpdate() override {
    config_->onConfigUpdate();
    return absl::OkStatus();
  }

private:
  const DynamicModuleTlsCertificateSelectorConfigSharedPtr config_;
  const Ssl::ServerContextConfig& server_config_;
};

/**
 * Factory that creates a dynamic module certificate selector for each upstream TLS context.
 */
class DynamicModuleUpstreamTlsCertificateSelectorFactory
    : public Ssl::UpstreamTlsCertificateSelectorFactory {
public:
  explicit DynamicModuleUpstreamTlsCertificateSelectorFactory(
      DynamicModuleTlsCertificateSelectorConfigSharedPtr config)
      : config_(std::move(config)) {}
  ~DynamicModuleUpstreamTlsCertificateSelectorFactory() override { config_->detachTlsConfig(); }

  // Ssl::UpstreamTlsCertificateSelectorFactory
  Ssl::UpstreamTlsCertificateSelectorPtr createUpstreamTlsCertificateSelector(
      Ssl::TlsCertificateSelectorContext& selector_context) override;
  // An SDS update creates a new TLS context, whose selector receives the new certificates, so only
  // the dynamic certificates are rebuilt here.
  absl::Status onConfigUpdate() override {
    config_->onConfigUpdate();
    return absl::OkStatus();
  }

private:
  const DynamicModuleTlsCertificateSelectorConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
