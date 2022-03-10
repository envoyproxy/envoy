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
  addCertificateProvider(absl::string_view name,
                         const envoy::config::core::v3::TypedExtensionConfig& config) PURE;

  virtual CertificateProviderSharedPtr getCertificateProvider(absl::string_view name) PURE;
};

using CertificateProviderManagerPtr = std::unique_ptr<CertificateProviderManager>;

} // namespace CertificateProvider
} // namespace Envoy
