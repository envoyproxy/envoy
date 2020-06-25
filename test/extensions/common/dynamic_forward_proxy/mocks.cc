#include "test/extensions/common/dynamic_forward_proxy/mocks.h"

using testing::_;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

MockDnsCacheResourceManager::MockDnsCacheResourceManager() {
  ON_CALL(*this, pendingRequests()).WillByDefault(ReturnRef(pending_requests_));
}
MockDnsCacheResourceManager::~MockDnsCacheResourceManager() = default;

MockDnsCache::MockDnsCache() {
  ON_CALL(*this, canCreateDnsRequest_(_)).WillByDefault(Return(nullptr));
}
MockDnsCache::~MockDnsCache() = default;

MockLoadDnsCacheEntryHandle::MockLoadDnsCacheEntryHandle() = default;
MockLoadDnsCacheEntryHandle::~MockLoadDnsCacheEntryHandle() { onDestroy(); }

MockDnsCacheManager::MockDnsCacheManager() {
  ON_CALL(*this, getCache(_)).WillByDefault(Return(dns_cache_));
}
MockDnsCacheManager::~MockDnsCacheManager() = default;

MockDnsHostInfo::MockDnsHostInfo() {
  ON_CALL(*this, address()).WillByDefault(ReturnPointee(&address_));
  ON_CALL(*this, resolvedHost()).WillByDefault(ReturnRef(resolved_host_));
}
MockDnsHostInfo::~MockDnsHostInfo() = default;

MockUpdateCallbacks::MockUpdateCallbacks() = default;
MockUpdateCallbacks::~MockUpdateCallbacks() = default;

MockLoadDnsCacheEntryCallbacks::MockLoadDnsCacheEntryCallbacks() = default;
MockLoadDnsCacheEntryCallbacks::~MockLoadDnsCacheEntryCallbacks() = default;

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
