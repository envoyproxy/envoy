#pragma once

#include <list>
#include <memory>
#include <string>

#include "envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3/config.pb.validate.h"
#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace DynamicModulesCertSelector {

using ConfigProto =
    envoy::extensions::transport_sockets::tls::cert_selectors::dynamic_modules::v3::Config;

// Resolved ABI function pointer types.
using OnCertSelectorConfigNewType = decltype(&envoy_dynamic_module_on_cert_selector_config_new);
using OnCertSelectorConfigDestroyType =
    decltype(&envoy_dynamic_module_on_cert_selector_config_destroy);
using OnCertSelectorNewType = decltype(&envoy_dynamic_module_on_cert_selector_new);
using OnCertSelectorDestroyType = decltype(&envoy_dynamic_module_on_cert_selector_destroy);
using OnCertSelectorSelectType = decltype(&envoy_dynamic_module_on_cert_selector_select);
using OnCertSelectorConfigUpdatedType =
    decltype(&envoy_dynamic_module_on_cert_selector_config_updated);

/**
 * Scope holder initialized first in the inheritance chain so that ``certScope()`` is usable
 * when the ``ServerContextImpl`` base constructor runs.
 */
class ScopeHolder {
public:
  explicit ScopeHolder(Stats::Scope& scope) : scope_(scope.createScope("")) {}
  Stats::Scope& certScope() const { return *scope_; }

private:
  Stats::ScopeSharedPtr scope_;
};

/**
 * A server TLS context built on the fly from PEM material returned by the dynamic module.
 *
 * ``add_selector=false`` is critical here: the parent ``ServerContextConfig`` carries this
 * dynamic-module selector itself, and constructing the inner context with ``add_selector=true``
 * would re-enter the selector factory and recurse indefinitely.
 */
class DynamicContext : public ScopeHolder,
                       public Extensions::TransportSockets::Tls::ServerContextImpl {
public:
  DynamicContext(Stats::Scope& scope, Server::Configuration::ServerFactoryContext& factory_context,
                 const Ssl::ServerContextConfig& tls_config,
                 const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status)
      : ScopeHolder(scope),
        ServerContextImpl(
            certScope(), tls_config,
            std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>{cert_config},
            /*add_selector=*/false, factory_context, nullptr, creation_status) {}

  const Ssl::TlsContext& tlsContext() const { return tls_contexts_[0]; }
};

using DynamicContextConstSharedPtr = std::shared_ptr<const DynamicContext>;

/**
 * Extends the TLS context lifetime through the duration of the handshake and connection.
 */
class Handle : public Ssl::SelectionHandle {
public:
  explicit Handle(DynamicContextConstSharedPtr ctx) : ctx_(std::move(ctx)) {}

private:
  DynamicContextConstSharedPtr ctx_;
};

/**
 * Per-worker LRU cache of compiled TLS contexts keyed by a module-supplied cache key.
 *
 * Each entry holds the compiled context plus an iterator into ``lru_`` so that touches
 * and evictions are O(1). ``lru_`` orders the keys with front = most-recently-used.
 */
struct ThreadLocalCerts : public ThreadLocal::ThreadLocalObject {
  struct Entry {
    DynamicContextConstSharedPtr ctx;
    std::list<std::string>::iterator lru_it;
  };
  absl::flat_hash_map<std::string, Entry> entries_;
  std::list<std::string> lru_;
};

/**
 * Shared configuration: owns the loaded module and the resolved ABI entry points. Held by the
 * factory and by every per-worker selector so the module outlives all selectors.
 */
class DynamicModuleCertSelectorConfig {
public:
  DynamicModuleCertSelectorConfig(std::string selector_name, std::string selector_config,
                                  Envoy::Extensions::DynamicModules::DynamicModulePtr module);
  ~DynamicModuleCertSelectorConfig();

  const std::string& selectorName() const { return selector_name_; }
  const std::string& selectorConfig() const { return selector_config_; }

  envoy_dynamic_module_type_cert_selector_config_module_ptr in_module_config_{nullptr};

  // Required ABI entry points (all non-null after a successful newDynamicModuleCertSelectorConfig).
  OnCertSelectorConfigDestroyType on_config_destroy_{nullptr};
  OnCertSelectorNewType on_new_{nullptr};
  OnCertSelectorDestroyType on_destroy_{nullptr};
  OnCertSelectorSelectType on_select_{nullptr};
  // Optional.
  OnCertSelectorConfigUpdatedType on_config_updated_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleCertSelectorConfig>>
  newDynamicModuleCertSelectorConfig(
      const std::string& selector_name, const std::string& selector_config,
      Envoy::Extensions::DynamicModules::DynamicModulePtr module);

  const std::string selector_name_;
  const std::string selector_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleCertSelectorConfigSharedPtr = std::shared_ptr<DynamicModuleCertSelectorConfig>;

absl::StatusOr<DynamicModuleCertSelectorConfigSharedPtr>
newDynamicModuleCertSelectorConfig(const std::string& selector_name,
                                   const std::string& selector_config,
                                   Envoy::Extensions::DynamicModules::DynamicModulePtr module);

/**
 * A TLS certificate selector backed by a dynamic module. One instance exists per worker per
 * parent TLS context.
 */
class DynamicModuleCertSelector : public Ssl::TlsCertificateSelector,
                                  protected Logger::Loggable<Logger::Id::connection> {
public:
  DynamicModuleCertSelector(DynamicModuleCertSelectorConfigSharedPtr config,
                            Ssl::TlsCertificateSelectorContext& selector_ctx,
                            ThreadLocal::TypedSlot<ThreadLocalCerts>& tls_cache,
                            Stats::Scope& stats_scope,
                            Server::Configuration::ServerFactoryContext& factory_context,
                            const Ssl::ServerContextConfig& tls_config, bool provides_certificates,
                            uint32_t max_cache_size);
  ~DynamicModuleCertSelector() override;

  // Ssl::TlsCertificateSelector
  bool providesCertificates() const override { return provides_certificates_; }

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    // findTlsContext is the QUIC selection path; QUIC is rejected at config time
    // by createTlsCertificateSelectorFactory, so this is unreachable.
    PANIC("findTlsContext not supported by dynamic module cert selector");
  }

  // ---- Accessors used by the ABI callback implementations ----
  // Valid only during a selectTlsContext() call.
  const SSL_CLIENT_HELLO* currentClientHello() const { return current_ch_; }
  bool isClientOcspCapable() const { return client_ocsp_capable_; }
  Network::TransportSocketCallbacks* currentCallbacks() const { return current_callbacks_; }

private:
  // Look up or compile an SSL_CTX for the supplied PEM material. If cache_key is empty,
  // the context is not cached.
  DynamicContextConstSharedPtr getOrCreateContext(absl::string_view cache_key,
                                                  absl::string_view cert_pem,
                                                  absl::string_view key_pem,
                                                  absl::Status& status);

  DynamicModuleCertSelectorConfigSharedPtr config_;
  envoy_dynamic_module_type_cert_selector_module_ptr in_module_selector_{nullptr};
  const std::vector<Ssl::TlsContext>& tls_contexts_;
  ThreadLocal::TypedSlot<ThreadLocalCerts>& tls_cache_;
  Stats::Scope& stats_scope_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const Ssl::ServerContextConfig& tls_config_;
  const bool provides_certificates_;
  const uint32_t max_cache_size_;

  // Request-scoped state valid only during selectTlsContext().
  const SSL_CLIENT_HELLO* current_ch_{nullptr};
  bool client_ocsp_capable_{false};
  Network::TransportSocketCallbacks* current_callbacks_{nullptr};
};

class DynamicModuleCertSelectorFactory : public Ssl::TlsCertificateSelectorFactory {
public:
  DynamicModuleCertSelectorFactory(DynamicModuleCertSelectorConfigSharedPtr config,
                                   std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalCerts>> cache,
                                   Stats::ScopeSharedPtr stats_scope,
                                   Server::Configuration::ServerFactoryContext& factory_context,
                                   const Ssl::ServerContextConfig& tls_config,
                                   bool provides_certificates, uint32_t max_cache_size);

  Ssl::TlsCertificateSelectorPtr create(Ssl::TlsCertificateSelectorContext&) override;
  absl::Status onConfigUpdate() override;

private:
  DynamicModuleCertSelectorConfigSharedPtr config_;
  std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalCerts>> tls_cache_;
  Stats::ScopeSharedPtr stats_scope_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const Ssl::ServerContextConfig& tls_config_;
  const bool provides_certificates_;
  const uint32_t max_cache_size_;
};

class DynamicModuleCertSelectorConfigFactory : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig& tls_config,
                                      bool for_quic) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override {
    return "envoy.tls.certificate_selectors.dynamic_modules";
  }
};

DECLARE_FACTORY(DynamicModuleCertSelectorConfigFactory);

} // namespace DynamicModulesCertSelector
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
