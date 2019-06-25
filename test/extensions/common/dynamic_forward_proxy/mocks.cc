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

MockLoadDnsCacheHandle::MockLoadDnsCacheHandle() = default;
MockLoadDnsCacheHandle::~MockLoadDnsCacheHandle() { onDestroy(); }

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

MockLoadDnsCacheCallbacks::MockLoadDnsCacheCallbacks() = default;
MockLoadDnsCacheCallbacks::~MockLoadDnsCacheCallbacks() = default;

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
