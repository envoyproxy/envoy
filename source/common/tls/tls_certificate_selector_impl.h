#pragma once

#include <openssl/ssl.h>

#include "envoy/ssl/handshaker.h"

#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/common/tls/stats.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Defined in server_context_impl.h
class ServerContextImpl;

/**
 * The default TLS context provider, selecting certificate based on SNI.
 */
class DefaultTlsCertificateSelectorImpl : public Ssl::TlsCertificateSelector,
                                          protected Logger::Loggable<Logger::Id::connection> {
public:
  DefaultTlsCertificateSelectorImpl(const Ssl::ServerContextConfig& config,
                                    Ssl::ContextSelectionCallback& cb);

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello,
                                        Ssl::CertSelectionCallbackPtr cb) override;

  // Finds the best matching context. The returned context will have the same lifetime as
  // ``ServerContextImpl``.
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view sni, bool client_ecdsa_capable, bool client_ocsp_capable,
                 bool* cert_matched_sni) override;

private:
  // Currently, at most one certificate of a given key type may be specified for each exact
  // server name or wildcard domain name.
  using PkeyTypesMap = absl::flat_hash_map<int, std::reference_wrapper<const Ssl::TlsContext>>;
  // Both exact server names and wildcard domains are part of the same map, in which wildcard
  // domains are prefixed with "." (i.e. ".example.com" for "*.example.com") to differentiate
  // between exact and wildcard entries.
  using ServerNamesMap = absl::flat_hash_map<std::string, PkeyTypesMap>;

  void populateServerNamesMap(const Ssl::TlsContext& ctx, const int pkey_id);

  Ssl::OcspStapleAction ocspStapleAction(const Ssl::TlsContext& ctx, bool client_ocsp_capable);

  // ServerContext own this selector, it's safe to use itself here.
  ServerContextImpl& server_ctx_;
  const std::vector<Ssl::TlsContext>& tls_contexts_;

  ServerNamesMap server_names_map_;
  bool has_rsa_{false};

  const Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy_;
  bool full_scan_certs_on_sni_mismatch_;
};

class TlsCertificateSelectorFactoryImpl : public Ssl::TlsCertificateSelectorFactory {
public:
  std::string name() const override { return "envoy.ssl.certificate_selector_factory.default"; }
  Ssl::TlsCertificateSelectorFactoryCb
  createTlsCertificateSelectorCb(const Protobuf::Message&,
                                 Server::Configuration::CommonFactoryContext&,
                                 ProtobufMessage::ValidationVisitor&) override {
    return [](const Ssl::ServerContextConfig& config, Ssl::ContextSelectionCallback& ctx) {
      return std::make_unique<DefaultTlsCertificateSelectorImpl>(config, ctx);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
};

DECLARE_FACTORY(TlsCertificateSelectorFactoryImpl);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
