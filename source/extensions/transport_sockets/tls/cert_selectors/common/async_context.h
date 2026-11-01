#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/server/factory_context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/stats/scope.h"

#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/server_context_impl.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {

class AsyncContext;
using AsyncContextConstSharedPtr = std::shared_ptr<const AsyncContext>;
using AsyncContextFactory = absl::AnyInvocable<AsyncContextConstSharedPtr(
    Stats::Scope&, Server::Configuration::ServerFactoryContext&, const Ssl::TlsCertificateConfig&,
    absl::Status&)>;

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

private:
  Stats::ScopeSharedPtr scope_;
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

} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
