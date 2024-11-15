#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/ssl/context_config.h"

#include "source/common/common/utility.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/stats.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

std::string getCertValidatorName(const Envoy::Ssl::CertificateValidationContextConfig* config);

class CertValidatorFactory : public Config::UntypedFactory {
public:
  virtual CertValidatorPtr
  createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                      Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tls.cert_validator"; }
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
