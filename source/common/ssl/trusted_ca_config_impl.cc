#include "common/ssl/trusted_ca_config_impl.h"

#include "common/common/empty_string.h"
#include "common/config/datasource.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

TrustedCaConfigImpl::TrustedCaConfigImpl(const envoy::api::v2::core::DataSource& config)
    : ca_cert_(Config::DataSource::read(config, true)),
      ca_cert_path_(Config::DataSource::getPath(config).value_or(
          ca_cert_.empty() ? EMPTY_STRING : INLINE_STRING)) {}

} // namespace Ssl
} // namespace Envoy
