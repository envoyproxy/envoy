#pragma once

#include <string>

#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"

namespace Envoy {
namespace CertificateProvider {

/**
 * A manager for certificate provider instances.
 */
class CertificateProviderManager {
public:
  virtual ~CertificateProviderManager() = default;

  virtual void
  addCertificateProvider(std::string name,
                         const envoy::config::core::v3::TypedExtensionConfig& config) PURE;

  virtual CertificateProviderSharedPtr getCertificateProvider(std::string name) PURE;
};

using CertificateProviderManagerPtr = std::unique_ptr<CertificateProviderManager>;

} // namespace CertificateProvider
} // namespace Envoy
