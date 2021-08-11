#include "source/common/ssl/certificate_validation_context_config_impl.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/datasource.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

CertificateValidationContextConfigImpl::CertificateValidationContextConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext& config,
    Api::Api& api)
    : ca_cert_(Config::DataSource::read(config.trusted_ca(), true, api)),
      ca_cert_path_(Config::DataSource::getPath(config.trusted_ca())
                        .value_or(ca_cert_.empty() ? EMPTY_STRING : INLINE_STRING)),
      certificate_revocation_list_(Config::DataSource::read(config.crl(), true, api)),
      certificate_revocation_list_path_(
          Config::DataSource::getPath(config.crl())
              .value_or(certificate_revocation_list_.empty() ? EMPTY_STRING : INLINE_STRING)),
      subject_alt_name_matchers_(config.match_subject_alt_names().begin(),
                                 config.match_subject_alt_names().end()),
      verify_certificate_hash_list_(config.verify_certificate_hash().begin(),
                                    config.verify_certificate_hash().end()),
      verify_certificate_spki_list_(config.verify_certificate_spki().begin(),
                                    config.verify_certificate_spki().end()),
      allow_expired_certificate_(config.allow_expired_certificate()),
      trust_chain_verification_(config.trust_chain_verification()),
      custom_validator_config_(
          config.has_custom_validator_config()
              ? absl::make_optional<envoy::config::core::v3::TypedExtensionConfig>(
                    config.custom_validator_config())
              : absl::nullopt),
      api_(api) {
  if (ca_cert_.empty() && custom_validator_config_ == absl::nullopt) {
    if (!certificate_revocation_list_.empty()) {
      throw EnvoyException(fmt::format("Failed to load CRL from {} without trusted CA",
                                       certificateRevocationListPath()));
    }
    if (!subject_alt_name_matchers_.empty()) {
      throw EnvoyException("SAN-based verification of peer certificates without "
                           "trusted CA is insecure and not allowed");
    }
    if (allow_expired_certificate_) {
      throw EnvoyException("Certificate validity period is always ignored without trusted CA");
    }
  }
}

} // namespace Ssl
} // namespace Envoy
