#include "cluster_manager_factory.h"

namespace Envoy {
namespace CertificateProvider {
MockCertificateProviderManager::MockCertificateProviderManager() = default;

MockCertificateProviderManager::~MockCertificateProviderManager() = default;
} // namespace CertificateProvider
namespace Upstream {
MockClusterManagerFactory::MockClusterManagerFactory() = default;

MockClusterManagerFactory::~MockClusterManagerFactory() = default;
} // namespace Upstream
} // namespace Envoy
