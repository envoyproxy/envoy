#include "library/common/extensions/cert_validator/platform_bridge/config.h"

#include "library/common/api/external.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

CertValidatorPtr PlatformBridgeCertValidatorFactory::createCertValidator(
    const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
    Server::Configuration::CommonFactoryContext& /*context*/) {
  return std::make_unique<PlatformBridgeCertValidator>(config, stats);
}

REGISTER_FACTORY(PlatformBridgeCertValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
