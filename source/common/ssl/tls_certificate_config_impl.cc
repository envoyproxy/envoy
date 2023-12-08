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
std::vector<uint8_t> readOcspStaple(const envoy::config::core::v3::DataSource& source,
                                    Api::Api& api) {
  std::string staple = Config::DataSource::read(source, true, api);
  if (source.specifier_case() ==
      envoy::config::core::v3::DataSource::SpecifierCase::kInlineString) {
    throwEnvoyExceptionOrPanic("OCSP staple cannot be provided via inline_string");
  }

  return {staple.begin(), staple.end()};
}
} // namespace

static const std::string INLINE_STRING = "<inline>";

TlsCertificateConfigImpl::TlsCertificateConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api)
    : certificate_chain_(Config::DataSource::read(config.certificate_chain(), true, api)),
      certificate_chain_path_(
          Config::DataSource::getPath(config.certificate_chain())
              .value_or(certificate_chain_.empty() ? EMPTY_STRING : INLINE_STRING)),
      private_key_(Config::DataSource::read(config.private_key(), true, api)),
      private_key_path_(Config::DataSource::getPath(config.private_key())
                            .value_or(private_key_.empty() ? EMPTY_STRING : INLINE_STRING)),
      pkcs12_(Config::DataSource::read(config.pkcs12(), true, api)),
      pkcs12_path_(Config::DataSource::getPath(config.pkcs12())
                       .value_or(pkcs12_.empty() ? EMPTY_STRING : INLINE_STRING)),
      password_(Config::DataSource::read(config.password(), true, api)),
      password_path_(Config::DataSource::getPath(config.password())
                         .value_or(password_.empty() ? EMPTY_STRING : INLINE_STRING)),
      ocsp_staple_(readOcspStaple(config.ocsp_staple(), api)),
      ocsp_staple_path_(Config::DataSource::getPath(config.ocsp_staple())
                            .value_or(ocsp_staple_.empty() ? EMPTY_STRING : INLINE_STRING)),
      private_key_method_(nullptr) {
  if (config.has_pkcs12()) {
    if (config.has_private_key()) {
      throwEnvoyExceptionOrPanic(
          fmt::format("Certificate configuration can't have both pkcs12 and private_key"));
    }
    if (config.has_certificate_chain()) {
      throwEnvoyExceptionOrPanic(
          fmt::format("Certificate configuration can't have both pkcs12 and certificate_chain"));
    }
    if (config.has_private_key_provider()) {
      throwEnvoyExceptionOrPanic(
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
        throwEnvoyExceptionOrPanic(fmt::format("Failed to load private key provider: {}",
                                               config.private_key_provider().provider_name()));
      }

      if (!private_key_method_->isAvailable()) {
        private_key_method_ = nullptr;
      }
    }
    if (certificate_chain_.empty()) {
      throwEnvoyExceptionOrPanic(
          fmt::format("Failed to load incomplete certificate from {}: certificate chain not set",
                      certificate_chain_path_));
    }

    if (private_key_.empty() && private_key_method_ == nullptr) {
      throwEnvoyExceptionOrPanic(
          fmt::format("Failed to load incomplete private key from path: {}", private_key_path_));
    }
  }
}

} // namespace Ssl
} // namespace Envoy
