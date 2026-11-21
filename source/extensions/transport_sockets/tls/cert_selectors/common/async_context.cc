#include "source/extensions/transport_sockets/tls/cert_selectors/common/async_context.h"

#include "source/common/common/assert.h"
#include "source/common/tls/default_tls_certificate_selector.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {

const Ssl::TlsContext& ServerAsyncContext::tlsContext() const { return tls_contexts_[0]; }
const Ssl::TlsContext& ClientAsyncContext::tlsContext() const { return tls_contexts_[0]; }

Ssl::SelectionResult serveTlsContext(const Ssl::TlsContext& tls_context, bool client_ocsp_capable,
                                     Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy,
                                     Ssl::SelectionHandleConstSharedPtr handle) {
  // Every action is handled with no default, so a new action fails to compile instead of serving.
  switch (ocspStapleAction(tls_context, client_ocsp_capable, ocsp_staple_policy)) {
  case Ssl::OcspStapleAction::Staple:
    return {.status = Ssl::SelectionResult::SelectionStatus::Success,
            .selected_ctx = &tls_context,
            .staple = true,
            .handle = std::move(handle)};
  case Ssl::OcspStapleAction::NoStaple:
  case Ssl::OcspStapleAction::ClientNotCapable:
    return {.status = Ssl::SelectionResult::SelectionStatus::Success,
            .selected_ctx = &tls_context,
            .handle = std::move(handle)};
  case Ssl::OcspStapleAction::Fail:
    return {.status = Ssl::SelectionResult::SelectionStatus::Failed};
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
