#pragma once

#include "envoy/registry/registry.h"

#include "source/extensions/transport_sockets/tls/cert_validator/factory.h"

#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge.pb.h"
#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge_cert_validator.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class PlatformBridgeCertValidatorFactory : public CertValidatorFactory,
                                           public Config::TypedFactory {
public:
  CertValidatorPtr createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                                       SslStats& stats, TimeSource& time_source) override;

  std::string name() const override {
    return "envoy_mobile.cert_validator.platform_bridge_cert_validator";
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy_mobile::extensions::cert_validator::platform_bridge::PlatformBridgeCertValidator>();
  }
  std::string category() const override { return "envoy.tls.cert_validator"; }
};

DECLARE_FACTORY(PlatformBridgeCertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
