#include "test/extensions/common/dynamic_forward_proxy/mocks.h"

using testing::_;
using testing::Return;
using testing::ReturnPointee;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

MockDnsCache::MockDnsCache() = default;
MockDnsCache::~MockDnsCache() = default;

MockLoadDnsCacheEntryHandle::MockLoadDnsCacheEntryHandle() = default;
MockLoadDnsCacheEntryHandle::~MockLoadDnsCacheEntryHandle() { onDestroy(); }

MockDnsCacheManager::MockDnsCacheManager() {
  ON_CALL(*this, getCache(_)).WillByDefault(Return(dns_cache_));
}
MockDnsCacheManager::~MockDnsCacheManager() = default;

MockDnsHostInfo::MockDnsHostInfo() {
  ON_CALL(*this, address()).WillByDefault(ReturnPointee(&address_));
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
