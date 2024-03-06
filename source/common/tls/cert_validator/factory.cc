#include "source/common/tls/cert_validator/factory.h"

#include "envoy/ssl/context_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

std::string getCertValidatorName(const Envoy::Ssl::CertificateValidationContextConfig* config) {
  return config != nullptr && config->customValidatorConfig().has_value()
             ? config->customValidatorConfig().value().name()
             : "envoy.tls.cert_validator.default";
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
