#include "source/common/ssl/tls_root_ca_certificate_config_impl.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/datasource.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

TlsRootCACertificateConfigImpl::TlsRootCACertificateConfigImpl(
    const envoy::extensions::transport_sockets::tls::v3::TlsRootCACertificate& config,
    // Server::Configuration::TransportSocketFactoryContext& factory_context,
    Api::Api& api)
    : cert_(Config::DataSource::read(config.cert(), true, api)),
      cert_path_(Config::DataSource::getPath(config.cert())
                     .value_or(cert_.empty() ? EMPTY_STRING : INLINE_STRING)),
      private_key_(Config::DataSource::read(config.private_key(), true, api)),
      private_key_path_(Config::DataSource::getPath(config.private_key())
                            .value_or(private_key_.empty() ? EMPTY_STRING : INLINE_STRING)) {

  if (cert_.empty()) {
    throw EnvoyException(fmt::format(
        "Failed to load incomplete certificate from {}: certificate chain not set", cert_path_));
  }
  if (private_key_.empty()) {
    throw EnvoyException(
        fmt::format("Failed to load incomplete private key from path: {}", private_key_path_));
  }
}

} // namespace Ssl
} // namespace Envoy
