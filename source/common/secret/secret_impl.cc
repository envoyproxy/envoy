#include "common/secret/secret_impl.h"

#include <string>

#include "common/common/assert.h"
#include "common/config/datasource.h"

namespace Envoy {
namespace Secret {

SecretImpl::SecretImpl(const envoy::api::v2::auth::Secret& config)
    : name_(config.name()), certificate_chain_(Config::DataSource::read(
                                config.tls_certificate().certificate_chain(), true)),
      private_key_(Config::DataSource::read(config.tls_certificate().private_key(), true)) {}

} // namespace Secret
} // namespace Envoy
