#include "source/extensions/certificate_selectors/on_demand/config.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

CertSelectionStats generateCertSelectionStats(Stats::Scope& store) {
  std::string prefix("aysnc_cert_selection.");
  return {ALL_CERT_SELECTION_STATS(POOL_COUNTER_PREFIX(store, prefix),
                                   POOL_GAUGE_PREFIX(store, prefix),
                                   POOL_HISTOGRAM_PREFIX(store, prefix))};
}

OnDemandTlsCertificateSelector::OnDemandTlsCertificateSelector(Stats::Scope& store, Ssl::TlsCertificateSelectorContext&)
      : stats_(generateCertSelectionStats(store)) {}

SecretManager::SecretManager(const ConfigProto& config, Server::Configuration::GenericFactoryContext& factory_context, const Ssl::ContextConfig& tls_config) :
    cert_name_(config.name()), factory_context_(factory_context), tls_config_(tls_config) {
  if (!cert_name_.empty()) {
    cert_provider_ = factory_context.serverFactoryContext()
                .secretManager()
                .findOrCreateTlsCertificateProvider(
                    config.config_source(), cert_name_,
                    factory_context.serverFactoryContext(), factory_context.initManager(), true);
    // loadCerts is called immediately if the cert is available, callback is invoked on the main thread.
    cert_callback_handle_ = cert_provider_->addUpdateCallback([this]() { return loadCerts(); });
  }
}

absl::Status SecretManager::loadCerts() {
  auto* secret = cert_provider_->secret();
  if (secret != nullptr) {
    auto config_or_error = Ssl::TlsCertificateConfigImpl::create(*secret, factory_context_, factory_context_.serverFactoryContext().api(), cert_name_);
    RETURN_IF_NOT_OK(config_or_error.status());
    cert_config_.emplace(*std::move(config_or_error));
  }
  return absl::OkStatus();
}

REGISTER_FACTORY(OnDemandTlsCertificateSelectorFactory, Ssl::TlsCertificateSelectorConfigFactory);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
