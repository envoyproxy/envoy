#pragma once

#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/tls/context_impl.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

#define ALL_CERT_SELECTION_STATS(COUNTER, GAUGE, HISTOGRAM) COUNTER(certificate_added)

struct CertSelectionStats {
  ALL_CERT_SELECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)
};

class AsyncContext;
using AsyncContextConstSharedPtr = std::shared_ptr<const AsyncContext>;
using ConfigProto = envoy::extensions::certificate_selectors::on_demand_secret::v3::Config;
using UpdateCb = std::function<absl::Status(absl::string_view, const Ssl::TlsCertificateConfig&)>;

class AsyncContextConfig {
public:
  AsyncContextConfig(absl::string_view cert_name,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const envoy::config::core::v3::ConfigSource& config_source,
                     OptRef<Init::Manager> init_manager, UpdateCb update_cb);

private:
  absl::Status loadCert();

  Server::Configuration::ServerFactoryContext& factory_context_;
  const std::string cert_name_;
  UpdateCb update_cb_;
  absl::optional<Ssl::TlsCertificateConfigImpl> cert_config_;
  const Secret::TlsCertificateConfigProviderSharedPtr cert_provider_;
  const Common::CallbackHandlePtr cert_callback_handle_;
};
using AsyncContextConfigConstPtr = std::unique_ptr<AsyncContextConfig>;

class AsyncContext : public Extensions::TransportSockets::Tls::ServerContextImpl {
public:
  AsyncContext(Stats::Scope& scope, Server::Configuration::ServerFactoryContext& factory_context,
               const Ssl::ServerContextConfig& tls_config,
               const Ssl::TlsCertificateConfig& cert_config,
               const std::vector<std::string>& server_names, absl::Status& creation_status)
      : ServerContextImpl(
            scope, tls_config,
            std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>{cert_config},
            server_names, factory_context, nullptr, creation_status) {}

  // @returns the low-level TLS context stored in this context.
  const Ssl::TlsContext& tlsContext() const;
};

class Handle : public Ssl::SelectionHandle {
public:
  // Synchronous handle constructor must extend the context lifetime since it holds the low TLS
  // context.
  explicit Handle(AsyncContextConstSharedPtr cert_context) : active_context_(cert_context) {}
  // Asynchronous handle constructor must also keep the callback for the secret manager.
  explicit Handle(Ssl::CertificateSelectionCallbackPtr&& cb) : cb_(std::move(cb)) {}
  void notify(AsyncContextConstSharedPtr cert_ctx);

private:
  // Captures the selected certificate data for the duration of the socket.
  AsyncContextConstSharedPtr active_context_;
  Ssl::CertificateSelectionCallbackPtr cb_;
};
using HandleSharedPtr = std::shared_ptr<Handle>;

// Maintains dynamic subscriptions to SDS secrets and converts them from the xDS form to the
// boringssl TLS contexts, while applying the parent TLS configuration.
class SecretManager : public std::enable_shared_from_this<SecretManager> {
public:
  SecretManager(const ConfigProto& config,
                Server::Configuration::GenericFactoryContext& factory_context,
                const Ssl::ServerContextConfig& tls_config);

  void addCertificateConfig(absl::string_view secret_name, HandleSharedPtr handle,
                            OptRef<Init::Manager> init_manager);
  absl::Status updateCertificate(absl::string_view secret_name,
                                 const Ssl::TlsCertificateConfig& cert_config);

  // Thread-safe.
  void setContext(absl::string_view secret_name, AsyncContextConstSharedPtr cert_ctx);
  absl::optional<AsyncContextConstSharedPtr> getContext(absl::string_view secret_name) const;
  HandleSharedPtr fetchCertificate(absl::string_view secret_name,
                                   Ssl::CertificateSelectionCallbackPtr&& cb);

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

using StaticNameConfigProto =
    envoy::extensions::certificate_selectors::on_demand_secret::v3::StaticName;
class StaticNameMapperFactory : public Ssl::TlsCertificateMapperConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& proto_config,
      Server::Configuration::GenericFactoryContext& factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<StaticNameConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_mappers.static_name"; }
};

DECLARE_FACTORY(StaticNameMapperFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
