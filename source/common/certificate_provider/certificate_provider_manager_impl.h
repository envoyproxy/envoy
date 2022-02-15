#pragma once

#include <string>

#include "envoy/api/api.h"
#include "envoy/certificate_provider/certificate_provider_manager.h"
#include "envoy/config/core/v3/extension.pb.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace CertificateProvider {

/**
 * A manager for certificate provider instances.
 */
class CertificateProviderManagerImpl : public CertificateProviderManager {
public:
  CertificateProviderManagerImpl(Api::Api& api);

  void addCertificateProvider(std::string name,
                              const envoy::config::core::v3::TypedExtensionConfig& config) override;

  Envoy::Extensions::CertificateProviders::CertificateProviderSharedPtr
  getCertificateProvider(std::string name) override;

private:
  absl::flat_hash_map<std::string,
                      Envoy::Extensions::CertificateProviders::CertificateProviderSharedPtr>
      certificate_provider_instances_;
  Api::Api& api_;
};

using CertificateProviderManagerPtr = std::unique_ptr<CertificateProviderManager>;

} // namespace CertificateProvider
} // namespace Envoy
