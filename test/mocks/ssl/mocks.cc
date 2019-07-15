#include "mocks.h"

namespace Envoy {
namespace Ssl {

MockContextManager::MockContextManager() {}
MockContextManager::~MockContextManager() {}

MockConnectionInfo::MockConnectionInfo() {}
MockConnectionInfo::~MockConnectionInfo() {}

MockClientContext::MockClientContext() {}
MockClientContext::~MockClientContext() {}

MockClientContextConfig::MockClientContextConfig() {}
MockClientContextConfig::~MockClientContextConfig() {}

MockServerContextConfig::MockServerContextConfig() {}
MockServerContextConfig::~MockServerContextConfig() {}

MockPrivateKeyMethodManager::MockPrivateKeyMethodManager() {}
MockPrivateKeyMethodManager::~MockPrivateKeyMethodManager() {}

MockPrivateKeyMethodProvider::MockPrivateKeyMethodProvider() {}
MockPrivateKeyMethodProvider::~MockPrivateKeyMethodProvider() {}

} // namespace Ssl
} // namespace Envoy
