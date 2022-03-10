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

  void addCertificateProvider(absl::string_view name,
                              const envoy::config::core::v3::TypedExtensionConfig& config) override;

  CertificateProviderSharedPtr getCertificateProvider(absl::string_view name) override;

private:
  absl::flat_hash_map<std::string, CertificateProviderSharedPtr> certificate_provider_instances_;
  Api::Api& api_;
};

} // namespace CertificateProvider
} // namespace Envoy
