#include "source/common/certificate_provider/certificate_provider_manager_impl.h"

#include <string>

#include "envoy/common/exception.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace CertificateProvider {

CertificateProviderManagerImpl::CertificateProviderManagerImpl(Api::Api& api) : api_(api) {}

void CertificateProviderManagerImpl::addCertificateProvider(
    absl::string_view name, const envoy::config::core::v3::TypedExtensionConfig& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  auto& cert_provider_factory =
      Envoy::Config::Utility::getAndCheckFactory<CertificateProviderFactory>(config);

  auto cert_provider_instance =
      cert_provider_factory.createCertificateProviderInstance(config, factory_context, api_);
  certificate_provider_instances_.emplace(name, cert_provider_instance);
}

Envoy::CertificateProvider::CertificateProviderSharedPtr
CertificateProviderManagerImpl::getCertificateProvider(absl::string_view name) {
  auto it = certificate_provider_instances_.find(name);
  if (it != certificate_provider_instances_.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

} // namespace CertificateProvider
} // namespace Envoy
