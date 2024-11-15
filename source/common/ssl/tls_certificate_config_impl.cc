#include "source/common/ssl/tls_certificate_config_impl.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/datasource.h"

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

absl::StatusOr<std::unique_ptr<TlsCertificateConfigImpl>> TlsCertificateConfigImpl::create(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api) {
  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<TlsCertificateConfigImpl> ret(
      new TlsCertificateConfigImpl(config, factory_context, api, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

TlsCertificateConfigImpl::TlsCertificateConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api,
    absl::Status& creation_status)
    : certificate_chain_(maybeSet(Config::DataSource::read(config.certificate_chain(), true, api),
                                  creation_status)),
      certificate_chain_path_(
          Config::DataSource::getPath(config.certificate_chain())
              .value_or(certificate_chain_.empty() ? EMPTY_STRING : INLINE_STRING)),
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
          factory_context.sslContextManager()
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
}

} // namespace Ssl
} // namespace Envoy
