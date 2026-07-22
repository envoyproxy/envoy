#include "source/common/ssl/tls_certificate_config_impl.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

namespace {

// Either return the supplied string, or set error and return an empty string
// string.
std::string maybeSet(absl::StatusOr<std::string> to_set, absl::Status error) {
  if (to_set.status().ok()) {
    return std::move(to_set.value());
  }
  error = to_set.status();
  return "";
}

std::vector<uint8_t> maybeReadOcspStaple(const envoy::config::core::v3::DataSource& source,
                                         Api::Api& api, absl::Status& creation_status) {
  auto staple_or_error = Config::DataSource::read(source, true, api);
  if (!staple_or_error.ok()) {
    creation_status = staple_or_error.status();
    return {};
  }
  const std::string& staple = staple_or_error.value();

  if (source.specifier_case() ==
      envoy::config::core::v3::DataSource::SpecifierCase::kInlineString) {
    creation_status =
        absl::InvalidArgumentError("OCSP staple cannot be provided via inline_string");
    return {};
  }

  return {staple.begin(), staple.end()};
}

} // namespace

static const std::string INLINE_STRING = "<inline>";

absl::StatusOr<TlsCertificateConfigImpl> TlsCertificateConfigImpl::create(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api,
    const std::string& certificate_name) {
  absl::Status creation_status = absl::OkStatus();
  TlsCertificateConfigImpl ret(config, factory_context, api, creation_status, certificate_name);
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

TlsCertificateConfigImpl::TlsCertificateConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api,
    absl::Status& creation_status, const std::string& certificate_name)
    : certificate_chain_(maybeSet(Config::DataSource::read(config.certificate_chain(), true, api),
                                  creation_status)),
      certificate_chain_path_(
          Config::DataSource::getPath(config.certificate_chain())
              .value_or(certificate_chain_.empty() ? EMPTY_STRING : INLINE_STRING)),
      certificate_name_(certificate_name),
      private_key_(
          maybeSet(Config::DataSource::read(config.private_key(), true, api), creation_status)),
      private_key_path_(Config::DataSource::getPath(config.private_key())
                            .value_or(private_key_.empty() ? EMPTY_STRING : INLINE_STRING)),
      pkcs12_(maybeSet(Config::DataSource::read(config.pkcs12(), true, api), creation_status)),
      pkcs12_path_(Config::DataSource::getPath(config.pkcs12())
                       .value_or(pkcs12_.empty() ? EMPTY_STRING : INLINE_STRING)),
      password_(maybeSet(Config::DataSource::read(config.password(), true, api), creation_status)),
      password_path_(Config::DataSource::getPath(config.password())
                         .value_or(password_.empty() ? EMPTY_STRING : INLINE_STRING)),
      ocsp_staple_(maybeReadOcspStaple(config.ocsp_staple(), api, creation_status)),
      ocsp_staple_path_(Config::DataSource::getPath(config.ocsp_staple())
                            .value_or(ocsp_staple_.empty() ? EMPTY_STRING : INLINE_STRING)),
      private_key_method_(nullptr) {
  // If creation_status was invalid as part of the data members init, there's no
  // need to continue with the update.
  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  if (config.has_pkcs12()) {
    if (config.has_private_key()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("Certificate configuration can't have both pkcs12 and private_key"));
    }
    if (config.has_certificate_chain()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("Certificate configuration can't have both pkcs12 and certificate_chain"));
    }
    if (config.has_private_key_provider()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("Certificate configuration can't have both pkcs12 and private_key_provider"));
    }
  } else {
    if (config.has_private_key_provider()) {
      private_key_method_ =
          factory_context.serverFactoryContext()
              .sslContextManager()
              .privateKeyMethodManager()
              .createPrivateKeyMethodProvider(config.private_key_provider(), factory_context);
      if (private_key_method_ == nullptr ||
          (!private_key_method_->isAvailable() && !config.private_key_provider().fallback())) {
        creation_status =
            absl::InvalidArgumentError(fmt::format("Failed to load private key provider: {}",
                                                   config.private_key_provider().provider_name()));
        return;
      }

      if (!private_key_method_->isAvailable()) {
        private_key_method_ = nullptr;
      }
    }
    if (certificate_chain_.empty()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("Failed to load incomplete certificate from {}: certificate chain not set",
                      certificate_chain_path_));
    }

    if (private_key_.empty() && private_key_method_ == nullptr) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("Failed to load incomplete private key from path: {}", private_key_path_));
    }
  }
  if (config.has_tls_params()) {
    using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
    const auto& p = config.tls_params();
    if (p.compliance_policies_size() > 1) {
      creation_status = absl::InvalidArgumentError(
          "Only one compliance policy may be specified per certificate tls_params");
      return;
    }
    tls_params_ = TlsParams{
        .min_protocol_version = p.tls_minimum_protocol_version(),
        .max_protocol_version = p.tls_maximum_protocol_version(),
        .cipher_suites = absl::StrJoin(p.cipher_suites(), ":"),
        .ecdh_curves = absl::StrJoin(p.ecdh_curves(), ":"),
        .signature_algorithms = absl::StrJoin(p.signature_algorithms(), ":"),
        .compliance_policy =
            p.compliance_policies_size() > 0
                ? std::optional<TlsProto::CompliancePolicy>(p.compliance_policies(0))
                : std::nullopt,
    };
    bssl::UniquePtr<SSL_CTX> validation_ctx(SSL_CTX_new(TLS_method()));
    if (!tls_params_->cipher_suites.empty() &&
        !SSL_CTX_set_strict_cipher_list(validation_ctx.get(), tls_params_->cipher_suites.c_str())) {
      std::vector<absl::string_view> ciphers =
          StringUtil::splitToken(tls_params_->cipher_suites, ":+![|]", false);
      std::vector<std::string> bad_ciphers;
      for (const auto& cipher : ciphers) {
        std::string cipher_str(cipher);
        if (absl::StartsWith(cipher_str, "-")) {
          cipher_str.erase(cipher_str.begin());
        }
        if (!SSL_CTX_set_strict_cipher_list(validation_ctx.get(), cipher_str.c_str())) {
          bad_ciphers.push_back(cipher_str);
        }
      }
      creation_status = absl::InvalidArgumentError(fmt::format(
          "Failed to initialize cipher suites {}. The following ciphers were rejected when tried "
          "individually: {}",
          tls_params_->cipher_suites, absl::StrJoin(bad_ciphers, ", ")));
      return;
    }
    if (!tls_params_->ecdh_curves.empty() &&
        !SSL_CTX_set1_curves_list(validation_ctx.get(), tls_params_->ecdh_curves.c_str())) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Failed to initialize ECDH curves ", tls_params_->ecdh_curves));
      return;
    }
    if (!tls_params_->signature_algorithms.empty() &&
        !SSL_CTX_set1_sigalgs_list(validation_ctx.get(),
                                   tls_params_->signature_algorithms.c_str())) {
      creation_status = absl::InvalidArgumentError(absl::StrCat(
          "Failed to initialize TLS signature algorithms ", tls_params_->signature_algorithms));
      return;
    }
    if (tls_params_->compliance_policy.has_value()) {
      using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
      switch (tls_params_->compliance_policy.value()) {
      case TlsProto::FIPS_202205:
        if (SSL_CTX_set_compliance_policy(validation_ctx.get(),
                                          ssl_compliance_policy_fips_202205) != 1) {
          creation_status = absl::InvalidArgumentError(
              "Failed to apply FIPS_202205 compliance policy in per-certificate tls_params");
          return;
        }
        break;
      // New policy values must be explicitly handled here before being accepted.
      default:
        creation_status =
            absl::InvalidArgumentError("Unknown compliance policy in per-certificate tls_params");
        return;
      }
    }
  }
}

} // namespace Ssl
} // namespace Envoy
