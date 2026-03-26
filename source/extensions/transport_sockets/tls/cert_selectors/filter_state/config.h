#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/tls/server_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace FilterState {

using ConfigProto =
    envoy::extensions::transport_sockets::tls::cert_selectors::filter_state::v3::Config;

/**
 * Base class that owns the stats scope. Initialized before ServerContextImpl in the
 * inheritance order so that certScope() is available when the base class constructor runs.
 */
class ScopeHolder {
public:
  explicit ScopeHolder(Stats::Scope& scope) : scope_(scope.createScope("")) {}
  Stats::Scope& certScope() const { return *scope_; }

private:
  Stats::ScopeSharedPtr scope_;
};

/**
 * A server TLS context created from a single dynamically-provided certificate.
 * Inherits ScopeHolder first to ensure the owned scope is initialized before
 * ServerContextImpl's constructor uses it.
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
            false, factory_context, nullptr, creation_status) {}

  const Ssl::TlsContext& tlsContext() const { return tls_contexts_[0]; }
};

using DynamicContextConstSharedPtr = std::shared_ptr<const DynamicContext>;

/**
 * Holds a shared_ptr to keep the DynamicContext (and its SSL_CTX) alive for the
 * duration of a TLS connection.
 */
class Handle : public Ssl::SelectionHandle {
public:
  explicit Handle(DynamicContextConstSharedPtr ctx) : ctx_(std::move(ctx)) {}

private:
  DynamicContextConstSharedPtr ctx_;
};

/**
 * Per-worker thread-local cache of compiled TLS contexts keyed by certificate name.
 */
struct ThreadLocalCerts : public ThreadLocal::ThreadLocalObject {
  absl::flat_hash_map<std::string, DynamicContextConstSharedPtr> ctx_by_name_;
};

/**
 * Certificate selector that reads PEM certificate chain and private key from connection
 * filter state. Certificates are compiled into TLS contexts and cached per-worker.
 */
class FilterStateCertSelector : public Ssl::TlsCertificateSelector,
                                protected Logger::Loggable<Logger::Id::connection> {
public:
  FilterStateCertSelector(Ssl::TlsCertificateMapperPtr&& mapper,
                          ThreadLocal::TypedSlot<ThreadLocalCerts>& cert_contexts,
                          Stats::Scope& scope,
                          Server::Configuration::ServerFactoryContext& factory_context,
                          const Ssl::ServerContextConfig& tls_config,
                          const std::string& cert_chain_key, const std::string& private_key_key,
                          uint32_t max_cache_size);

  // Ssl::TlsCertificateSelector
  bool providesCertificates() const override { return true; }
  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("Not supported with QUIC");
  }

private:
  DynamicContextConstSharedPtr createContext(const std::string& cert_pem,
                                             const std::string& key_pem);

  Ssl::TlsCertificateMapperPtr mapper_;
  ThreadLocal::TypedSlot<ThreadLocalCerts>& cert_contexts_;
  Stats::ScopeSharedPtr stats_scope_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const Ssl::ServerContextConfig& tls_config_;
  const std::string cert_chain_key_;
  const std::string private_key_key_;
  const uint32_t max_cache_size_;
};

class FilterStateCertSelectorFactory : public Ssl::TlsCertificateSelectorFactory {
public:
  FilterStateCertSelectorFactory(
      Ssl::TlsCertificateMapperFactory&& mapper_factory,
      std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalCerts>>&& cert_contexts,
      Stats::ScopeSharedPtr scope, Server::Configuration::ServerFactoryContext& factory_context,
      const Ssl::ServerContextConfig& tls_config, const std::string& cert_chain_key,
      const std::string& private_key_key, uint32_t max_cache_size)
      : mapper_factory_(std::move(mapper_factory)), cert_contexts_(std::move(cert_contexts)),
        scope_(std::move(scope)), factory_context_(factory_context), tls_config_(tls_config),
        cert_chain_key_(cert_chain_key), private_key_key_(private_key_key),
        max_cache_size_(max_cache_size) {}

  Ssl::TlsCertificateSelectorPtr create(Ssl::TlsCertificateSelectorContext&) override;
  absl::Status onConfigUpdate() override;

private:
  Ssl::TlsCertificateMapperFactory mapper_factory_;
  std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalCerts>> cert_contexts_;
  Stats::ScopeSharedPtr scope_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const Ssl::ServerContextConfig& tls_config_;
  const std::string cert_chain_key_;
  const std::string private_key_key_;
  const uint32_t max_cache_size_;
};

class FilterStateCertSelectorConfigFactory : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& proto_config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig& tls_config,
                                      bool for_quic) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.tls.certificate_selectors.filter_state"; }
};

DECLARE_FACTORY(FilterStateCertSelectorConfigFactory);

} // namespace FilterState
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
