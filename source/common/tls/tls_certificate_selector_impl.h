#pragma once

#include <openssl/ssl.h>

#include "envoy/ssl/handshaker.h"

#include "source/common/tls/context_impl.h"
#include "source/common/tls/stats.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

/**
 * The default TLS context provider, selecting certificate based on SNI.
 */
class TlsCertificateSelectorImpl : public Ssl::TlsCertificateSelector,
                                   protected Logger::Loggable<Logger::Id::connection> {
public:
  TlsCertificateSelectorImpl(Ssl::ContextSelectionCallbackWeakPtr cb) : cb_(cb){};

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello,
                                        Ssl::CertSelectionCallbackPtr cb) override;

private:
  Ssl::ContextSelectionCallbackWeakPtr cb_;
};

TlsCertificateSelectorSharedPtr
TlsCertificateSelectorFactoryCbImpl(Ssl::ContextSelectionCallbackWeakPtr cb);

} // namespace Ssl
} // namespace Envoy
