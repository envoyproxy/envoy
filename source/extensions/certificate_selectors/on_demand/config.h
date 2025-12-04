#pragma once

#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/server_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

#define ALL_CERT_SELECTION_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(cert_requested)                                                                          \
  GAUGE(cert_active, Accumulate)

struct CertSelectionStats {
  ALL_CERT_SELECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)
};

class AsyncContext;
using AsyncContextConstSharedPtr = std::shared_ptr<const AsyncContext>;
using ConfigProto = envoy::extensions::certificate_selectors::on_demand_secret::v3::Config;
using UpdateCb = std::function<absl::Status(absl::string_view, const Ssl::TlsCertificateConfig&)>;
using RemoveCb = std::function<absl::Status(absl::string_view)>;

class AsyncContextConfig {
public:
  AsyncContextConfig(absl::string_view cert_name,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const envoy::config::core::v3::ConfigSource& config_source,
                     OptRef<Init::Manager> init_manager, UpdateCb update_cb, RemoveCb remove_cb);

private:
  absl::Status loadCert();

  Server::Configuration::ServerFactoryContext& factory_context_;
  const std::string cert_name_;
  absl::optional<Ssl::TlsCertificateConfigImpl> cert_config_;
  const Secret::TlsCertificateConfigProviderSharedPtr cert_provider_;
  UpdateCb update_cb_;
  const Common::CallbackHandlePtr update_cb_handle_;
  RemoveCb remove_cb_;
  const Common::CallbackHandlePtr remove_cb_handle_;
};
using AsyncContextConfigConstPtr = std::unique_ptr<AsyncContextConfig>;

class AsyncContext : public Extensions::TransportSockets::Tls::ServerContextImpl {
public:
  AsyncContext(Stats::Scope& scope, Server::Configuration::ServerFactoryContext& factory_context,
               const Ssl::ServerContextConfig& tls_config,
               const Ssl::TlsCertificateConfig& cert_config, absl::Status& creation_status)
      : ServerContextImpl(
            scope, tls_config,
            std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>{cert_config},
            false, factory_context, nullptr, creation_status) {}

  // @returns the low-level TLS context stored in this context.
  const Ssl::TlsContext& tlsContext() const;
};

class Handle : public Ssl::SelectionHandle {
public:
  // Synchronous handle constructor must extend the context lifetime since it holds the low TLS
  // context.
  explicit Handle(AsyncContextConstSharedPtr cert_context) : active_context_(cert_context) {}
  // Asynchronous handle constructor must also keep the callback for the secret manager.
  Handle(Ssl::CertificateSelectionCallbackPtr&& cb, bool client_ocsp_capable)
      : cb_(std::move(cb)), client_ocsp_capable_(client_ocsp_capable) {}
  void notify(AsyncContextConstSharedPtr cert_ctx,
              Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy);

private:
  // Captures the selected certificate data for the duration of the socket.
  AsyncContextConstSharedPtr active_context_;
  Ssl::CertificateSelectionCallbackPtr cb_;
  bool client_ocsp_capable_{false};
};
using HandleSharedPtr = std::shared_ptr<Handle>;

// Maintains dynamic subscriptions to SDS secrets and converts them from the xDS form to the
// boringssl TLS contexts, while applying the parent TLS configuration.
class SecretManager : public std::enable_shared_from_this<SecretManager>,
                      protected Logger::Loggable<Logger::Id::secret> {
public:
  SecretManager(const ConfigProto& config,
                Server::Configuration::GenericFactoryContext& factory_context,
                const Ssl::ServerContextConfig& tls_config);

  /**
   * Start a subscription to the secret and register a handle on updates.
   */
  void addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                            OptRef<Init::Manager> init_manager);
  /**
   * Handle an updated certificate config by notifying any pending connections.
   */
  absl::Status updateCertificate(absl::string_view secret_name,
                                 const Ssl::TlsCertificateConfig& cert_config);

  /**
   * Delete any cached state for the secret since it is removed by the xDS server.
   */
  absl::Status removeCertificateConfig(absl::string_view) { return absl::OkStatus(); }

  // Below methods are thread-safe, unlike prior ones which are only safe on the main thread.
  void setContext(absl::string_view secret_name, AsyncContextConstSharedPtr cert_ctx);
  absl::optional<AsyncContextConstSharedPtr> getContext(absl::string_view secret_name) const;
  HandleSharedPtr fetchCertificate(absl::string_view secret_name,
                                   Ssl::CertificateSelectionCallbackPtr&& cb,
                                   bool client_ocsp_capable);
  Ssl::ServerContextConfig::OcspStaplePolicy ocspStaplePolicy() const;

private:
  const Stats::ScopeSharedPtr stats_scope_;
  CertSelectionStats stats_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  const Ssl::ServerContextConfig& tls_config_;

  // Main-thread accessible context config subscriptions and callbacks.
  struct CacheEntry {
    AsyncContextConfigConstPtr cert_config_;
    AsyncContextConstSharedPtr cert_context_;
    std::vector<std::weak_ptr<Handle>> callbacks_;
  };
  // TODO(kuat): Needs GC.
  absl::flat_hash_map<std::string, CacheEntry> cache_;

  // Lock-free map to retrieve ready TLS contexts by name.
  struct ThreadLocalCerts : public ThreadLocal::ThreadLocalObject {
    absl::flat_hash_map<std::string, AsyncContextConstSharedPtr> ctx_by_name_;
  };
  ThreadLocal::TypedSlot<ThreadLocalCerts> cert_contexts_;
};

// An asynchronous certificate selector is created for each TLS socket on each worker.
class AsyncSelector : public Ssl::TlsCertificateSelector,
                      protected Logger::Loggable<Logger::Id::connection> {
public:
  AsyncSelector(Ssl::TlsCertificateMapper&& mapper, std::shared_ptr<SecretManager> secret_manager)
      : mapper_(std::move(mapper)), secret_manager_(secret_manager) {}

  bool providesCertificates() const override { return true; }
  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO&,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("Not supported with QUIC");
  };

private:
  Ssl::TlsCertificateMapper mapper_;
  std::shared_ptr<SecretManager> secret_manager_;
};

class OnDemandTlsCertificateSelectorFactory : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactory>
  createTlsCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig& tls_config,
                                      bool for_quic) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_selectors.on_demand_secret"; }
};

DECLARE_FACTORY(OnDemandTlsCertificateSelectorFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
