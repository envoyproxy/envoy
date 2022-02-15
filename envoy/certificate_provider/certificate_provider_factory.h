#pragma once

#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/registry/registry.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server
namespace CertificateProvider {

class CertificateProviderFactory : public Config::TypedFactory {
public:
  virtual Envoy::CertificateProvider::CertificateProviderSharedPtr
  createCertificateProviderInstance(
      const envoy::config::core::v3::TypedExtensionConfig& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context, Api::Api& api) PURE;

  std::string category() const override { return "envoy.certificate_providers"; }
};

} // namespace CertificateProvider
} // namespace Envoy
