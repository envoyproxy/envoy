#pragma once

#include <string>

#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/singleton/instance.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server
namespace CertificateProvider {

/**
 * A manager for certificate provider instances.
 */
class CertificateProviderManager : public Singleton::Instance {
public:
  ~CertificateProviderManager() override = default;

  virtual void addCertificateProvider(
      absl::string_view name, const envoy::config::core::v3::TypedExtensionConfig& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context) PURE;

  virtual CertificateProviderSharedPtr getCertificateProvider(absl::string_view name) PURE;
};

using CertificateProviderManagerPtr = std::unique_ptr<CertificateProviderManager>;

} // namespace CertificateProvider
} // namespace Envoy
