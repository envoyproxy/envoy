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

/**
 * The default TLS context provider, selecting certificate based on SNI.
 */
class TlsCertificateSelectorImpl : public Ssl::TlsCertificateSelector,
                                   protected Logger::Loggable<Logger::Id::connection> {
public:
  TlsCertificateSelectorImpl(Ssl::ContextSelectionCallbackWeakPtr cb) : cb_(cb){};

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello,
                                        Ssl::CertSelectionCallbackSharedPtr cb) override;

private:
  Ssl::ContextSelectionCallbackWeakPtr cb_;
};

class TlsCertificateSelectorFactoryImpl : public Ssl::TlsCertificateSelectorFactory {
public:
  std::string name() const override { return "envoy.ssl.certificate_selector_factory.default"; }
  Ssl::TlsCertificateSelectorFactoryCb
  createTlsCertificateSelectorCb(const ProtobufWkt::Any&,
                                 Ssl::TlsCertificateSelectorFactoryContext&,
                                 ProtobufMessage::ValidationVisitor&) override {
    return [](Ssl::ContextSelectionCallbackWeakPtr ctx) {
      return std::make_unique<TlsCertificateSelectorImpl>(ctx);
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
