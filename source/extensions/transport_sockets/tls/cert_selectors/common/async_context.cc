#include "source/extensions/transport_sockets/tls/cert_selectors/common/async_context.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {

const Ssl::TlsContext& ServerAsyncContext::tlsContext() const { return tls_contexts_[0]; }
const Ssl::TlsContext& ClientAsyncContext::tlsContext() const { return tls_contexts_[0]; }

} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
