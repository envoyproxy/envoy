#include "library/common/extensions/cert_validator/platform_bridge/config.h"

#include "library/common/api/external.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

CertValidatorPtr PlatformBridgeCertValidatorFactory::createCertValidator(
    const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
    TimeSource& /*time_source*/) {
  if (platform_validator_ == nullptr) {
    platform_validator_ =
        static_cast<envoy_cert_validator*>(Api::External::retrieveApi("platform_cert_validator"));
  }
  return std::make_unique<PlatformBridgeCertValidator>(config, stats, platform_validator_);
}

REGISTER_FACTORY(PlatformBridgeCertValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
