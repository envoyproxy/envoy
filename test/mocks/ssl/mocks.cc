#include "mocks.h"

namespace Envoy {
namespace Ssl {

MockContextManager::MockContextManager() = default;
MockContextManager::~MockContextManager() = default;

MockConnectionInfo::MockConnectionInfo() = default;
MockConnectionInfo::~MockConnectionInfo() = default;

MockClientContext::MockClientContext() = default;
MockClientContext::~MockClientContext() = default;

MockClientContextConfig::MockClientContextConfig() = default;
MockClientContextConfig::~MockClientContextConfig() = default;

MockServerContextConfig::MockServerContextConfig() = default;
MockServerContextConfig::~MockServerContextConfig() = default;

MockPrivateKeyMethodManager::MockPrivateKeyMethodManager() = default;
MockPrivateKeyMethodManager::~MockPrivateKeyMethodManager() = default;

MockPrivateKeyMethodProvider::MockPrivateKeyMethodProvider() = default;
MockPrivateKeyMethodProvider::~MockPrivateKeyMethodProvider() = default;

MockTlsCertificateConfigProvidersFactory::MockTlsCertificateConfigProvidersFactory() = default;
MockTlsCertificateConfigProvidersFactory::~MockTlsCertificateConfigProvidersFactory() = default;

MockCertificateValicationContextConfigProviderFactory::
    MockCertificateValicationContextConfigProviderFactory() = default;
MockCertificateValicationContextConfigProviderFactory::
    ~MockCertificateValicationContextConfigProviderFactory() = default;

} // namespace Ssl
} // namespace Envoy
