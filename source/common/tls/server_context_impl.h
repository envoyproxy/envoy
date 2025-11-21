#pragma once

#include <openssl/safestack.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/default_tls_certificate_selector.h"
#include "source/common/tls/ocsp/ocsp.h"
#include "source/common/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche/quic/core/crypto/proof_source.h"
#endif

namespace Envoy {

namespace Extensions {
namespace TransportSockets {
namespace Tls {

Ssl::CurveNIDVector getClientCurveNIDSupported(CBS& cbs);

class ServerContextImpl : public ContextImpl,
                          public Envoy::Ssl::ServerContext,
                          public Envoy::Ssl::TlsCertificateSelectorContext {
public:
  static absl::StatusOr<std::unique_ptr<ServerContextImpl>>
  create(Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
         const std::vector<std::string>& server_names,
         Server::Configuration::CommonFactoryContext& factory_context,
         Ssl::ContextAdditionalInitFunc additional_init);

  // Ssl::TlsCertificateSelectorContext
  // The returned vector has the same life-time as the Ssl::TlsCertificateSelectorContext.
  const std::vector<Ssl::TlsContext>& getTlsContexts() const override { return tls_contexts_; };

  // Select the TLS certificate context in SSL_CTX_set_select_certificate_cb() callback with
  // ClientHello details. This is made public for use by custom TLS extensions who want to
  // manually create and use this as a client hello callback.
  enum ssl_select_cert_result_t selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello);

  // Finds the best matching context. The returned context will have the same lifetime as
  // this ``ServerContextImpl``.
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view sni, const Ssl::CurveNIDVector& client_ecdsa_capable,
                 bool client_ocsp_capable, bool* cert_matched_sni);

  Ssl::CurveNIDVector getClientEcdsaCapabilities(const SSL_CLIENT_HELLO& ssl_client_hello) const;
  bool isClientOcspCapable(const SSL_CLIENT_HELLO& ssl_client_hello) const;

private:
  ServerContextImpl(Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
                    const std::vector<std::string>& server_names,
                    Server::Configuration::CommonFactoryContext& factory_context,
                    Ssl::ContextAdditionalInitFunc additional_init, absl::Status& creation_status);
  using SessionContextID = std::array<uint8_t, SSL_MAX_SSL_SESSION_ID_LENGTH>;

  int alpnSelectCallback(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                         unsigned int inlen);
  int sessionTicketProcess(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                           HMAC_CTX* hmac_ctx, int encrypt);

  absl::StatusOr<SessionContextID>
  generateHashForSessionContextId(const std::vector<std::string>& server_names);

  Ssl::TlsCertificateSelectorPtr tls_certificate_selector_;
  const std::vector<Envoy::Ssl::ServerContextConfig::SessionTicketKey> session_ticket_keys_;
  const Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy_;
};

class ServerContextFactoryImpl : public ServerContextFactory {
public:
  std::string name() const override { return "envoy.ssl.server_context_factory.default"; }
  absl::StatusOr<Ssl::ServerContextSharedPtr>
  createServerContext(Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
                      const std::vector<std::string>& server_names,
                      Server::Configuration::CommonFactoryContext& factory_context,
                      Ssl::ContextAdditionalInitFunc additional_init) override;
};

DECLARE_FACTORY(ServerContextFactoryImpl);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
