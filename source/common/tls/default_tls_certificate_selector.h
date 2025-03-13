#pragma once

#include <openssl/ssl.h>

#include "envoy/ssl/handshaker.h"

#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/common/tls/stats.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Defined in server_context_impl.h
class ServerContextImpl;

/**
 * The default TLS context provider, selecting certificate based on SNI.
 */
class DefaultTlsCertificateSelector : public Ssl::TlsCertificateSelector,
                                      protected Logger::Loggable<Logger::Id::connection> {
public:
  DefaultTlsCertificateSelector(const Ssl::ServerContextConfig& config,
                                Ssl::TlsCertificateSelectorContext& selector_ctx);

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  // Finds the best matching context. The returned context will have the same lifetime as
  // ``ServerContextImpl``.
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view sni, const Ssl::CurveNIDVector& client_ecdsa_capabilities,
                 bool client_ocsp_capable, bool* cert_matched_sni) override;

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

class TlsCertificateSelectorConfigFactoryImpl : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  std::string name() const override { return "envoy.tls.certificate_selectors.default"; }
  Ssl::TlsCertificateSelectorFactory createTlsCertificateSelectorFactory(
      const Protobuf::Message&, Server::Configuration::CommonFactoryContext&,
      ProtobufMessage::ValidationVisitor&, absl::Status&, bool) override {
    return [](const Ssl::ServerContextConfig& config,
              Ssl::TlsCertificateSelectorContext& selector_ctx) {
      return std::make_unique<DefaultTlsCertificateSelector>(config, selector_ctx);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  static Ssl::TlsCertificateSelectorConfigFactory* getDefaultTlsCertificateSelectorConfigFactory() {
    static TlsCertificateSelectorConfigFactoryImpl default_tls_certificate_selector_config_factory;
    return &default_tls_certificate_selector_config_factory;
  }
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
