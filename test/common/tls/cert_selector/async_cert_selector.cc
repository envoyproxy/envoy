#include "test/common/tls/cert_selector/async_cert_selector.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Ssl::SelectionResult
AsyncTlsCertificateSelector::selectTlsContext(const SSL_CLIENT_HELLO&,
                                              Ssl::CertificateSelectionCallbackPtr cb) {
  ENVOY_LOG_MISC(info, "debug: select context");

  if (mode_ == "sync") {
    auto& tls_context = selector_ctx_.getTlsContexts()[0];
    return {Ssl::SelectionResult::SelectionStatus::Success, &tls_context, false};
  }

  if (mode_ == "async") {
    ENVOY_LOG_MISC(info, "debug: select cert async");
    cb_ = std::move(cb);
    cb_->dispatcher().post([this] { selectTlsContextAsync(); });
    return {Ssl::SelectionResult::SelectionStatus::Pending, nullptr, false};
  }

  if (mode_ == "sleep") {
    ENVOY_LOG_MISC(info, "debug: select cert sleep");
    // select cert async after 1000ms
    cb_ = std::move(cb);
    selection_timer_ = cb_->dispatcher().createTimer([this] { selectTlsContextAsync(); });
    selection_timer_->enableTimer(std::chrono::milliseconds(1000));
    return {Ssl::SelectionResult::SelectionStatus::Pending, nullptr, false};
  }

  return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
};

void AsyncTlsCertificateSelector::selectTlsContextAsync() {
  ENVOY_LOG_MISC(info, "debug: select cert async done");
  // choose the first one.
  auto& tls_context = selector_ctx_.getTlsContexts()[0];
  cb_->onCertificateSelectionResult(tls_context, false);
}

REGISTER_FACTORY(AsyncTlsCertificateSelectorFactory, Ssl::TlsCertificateSelectorConfigFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
