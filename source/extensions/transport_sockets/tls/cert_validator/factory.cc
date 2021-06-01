#include "extensions/transport_sockets/tls/cert_validator/factory.h"

#include "envoy/ssl/context_config.h"

#include "extensions/transport_sockets/tls/cert_validator/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

std::string getCertValidatorName(const Envoy::Ssl::CertificateValidationContextConfig* config) {
  return config != nullptr && config->customValidatorConfig().has_value()
             ? config->customValidatorConfig().value().name()
             : CertValidatorNames::get().Default;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
