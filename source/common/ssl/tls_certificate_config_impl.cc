#include "common/ssl/tls_certificate_config_impl.h"

#include "envoy/common/exception.h"

#include "common/config/datasource.h"

namespace Envoy {
namespace Ssl {

TlsCertificateConfigImpl::TlsCertificateConfigImpl(
    const envoy::api::v2::auth::TlsCertificate& config)
    : certificate_chain_(Config::DataSource::read(config.certificate_chain(), true)),
      private_key_(Config::DataSource::read(config.private_key(), true)) {}

} // namespace Ssl
} // namespace Envoy
