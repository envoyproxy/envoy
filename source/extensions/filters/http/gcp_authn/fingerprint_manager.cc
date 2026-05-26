#include "source/extensions/filters/http/gcp_authn/fingerprint_manager.h"

#include "envoy/secret/secret_manager.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

FingerprintManager::FingerprintManager(
    const envoy::extensions::filters::http::gcp_authn::v3::TokenBindingConfig& config,
    Server::Configuration::FactoryContext& context)
    : config_(config), context_(context),
      tls_slot_(context.serverFactoryContext().threadLocal()) {

  tls_slot_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalFingerprint>(""); });

  for (const auto& matcher : config_.client_certificate_san_matchers()) {
    san_matchers_.emplace_back(matcher, context_.serverFactoryContext());
  }

  const auto& client_cert = config_.client_certificate();
  if (client_cert.has_sds_config()) {
    tls_cert_provider_ =
        context.serverFactoryContext().secretManager().findOrCreateTlsCertificateProvider(
            client_cert.sds_config(), client_cert.name(), context.serverFactoryContext(),
            context.initManager(), true);
  } else {
    tls_cert_provider_ =
        context.serverFactoryContext().secretManager().findStaticTlsCertificateProvider(
            client_cert.name());
  }

  if (tls_cert_provider_ != nullptr) {
    tls_cert_update_handle_ = tls_cert_provider_->addUpdateCallback([this]() {
      updateFingerprint();
      return absl::OkStatus();
    });
    updateFingerprint();
  } else {
    ENVOY_LOG_MISC(warn, "TlsCertificateConfigProvider not found for {}", client_cert.name());
  }
}

absl::optional<std::string> FingerprintManager::fingerprint() const {
  auto slot = tls_slot_.get();
  if (slot.has_value() && !slot->fingerprint_.empty()) {
    return slot->fingerprint_;
  }
  return absl::nullopt;
}

void FingerprintManager::updateFingerprint() {
  std::string fingerprint = "";
  if (tls_cert_provider_ != nullptr) {
    auto fingerprint_or_error = getBase64EncodedCertificateFingerprint(
        tls_cert_provider_, san_matchers_, context_.serverFactoryContext().api());
    if (fingerprint_or_error.ok()) {
      fingerprint = fingerprint_or_error.value();
    } else {
      ENVOY_LOG_MISC(warn, "Failed to get certificate fingerprint: {}",
                     fingerprint_or_error.status().message());
    }
  }

  tls_slot_.set([fingerprint](Envoy::Event::Dispatcher&) {
    return std::make_shared<ThreadLocalFingerprint>(fingerprint);
  });
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
