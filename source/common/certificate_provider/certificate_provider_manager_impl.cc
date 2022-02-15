#include "source/common/certificate_provider/certificate_provider_manager_impl.h"

#include <string>

#include "envoy/common/exception.h"

#include "source/common/config/utility.h"
#include "source/extensions/certificate_providers/factory.h"

namespace Envoy {
namespace CertificateProvider {

CertificateProviderManagerImpl::CertificateProviderManagerImpl(Api::Api& api) : api_(api) {}

void CertificateProviderManagerImpl::addCertificateProvider(
    std::string name, const envoy::config::core::v3::TypedExtensionConfig& config) {
  auto& cert_provider_factory = Envoy::Config::Utility::getAndCheckFactory<
      Extensions::CertificateProviders::CertificateProviderFactory>(config);

  auto cert_provider_instance =
      cert_provider_factory.createCertificateProviderInstance(config, api_);
  certificate_provider_instances_.emplace(name, cert_provider_instance);
}

Envoy::Extensions::CertificateProviders::CertificateProviderSharedPtr
CertificateProviderManagerImpl::getCertificateProvider(std::string name) {
  auto it = certificate_provider_instances_.find(name);
  if (it != certificate_provider_instances_.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

} // namespace CertificateProvider
} // namespace Envoy
