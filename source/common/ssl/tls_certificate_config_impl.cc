#include "common/ssl/tls_certificate_config_impl.h"

#include "envoy/common/exception.h"

#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/config/datasource.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

TlsCertificateConfigImpl::TlsCertificateConfigImpl(
    const envoy::api::v2::auth::TlsCertificate& config, Api::Api& api)
    : certificate_chain_(Config::DataSource::read(config.certificate_chain(), true, api)),
      certificate_chain_path_(
          Config::DataSource::getPath(config.certificate_chain())
              .value_or(certificate_chain_.empty() ? EMPTY_STRING : INLINE_STRING)),
      private_key_(Config::DataSource::read(config.private_key(), true, api)),
      private_key_path_(Config::DataSource::getPath(config.private_key())
                            .value_or(private_key_.empty() ? EMPTY_STRING : INLINE_STRING)),
      password_(Config::DataSource::read(config.password(), true, api)),
      password_path_(Config::DataSource::getPath(config.password())
                         .value_or(password_.empty() ? EMPTY_STRING : INLINE_STRING)) {

  if (certificate_chain_.empty() || private_key_.empty()) {
    throw EnvoyException(fmt::format("Failed to load incomplete certificate from {}, {}",
                                     certificate_chain_path_, private_key_path_));
  }
}

} // namespace Ssl
} // namespace Envoy
