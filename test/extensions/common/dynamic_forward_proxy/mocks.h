#pragma once

#include "extensions/common/dynamic_forward_proxy/dns_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class MockDnsCache : public DnsCache {
public:
  MockDnsCache();
  ~MockDnsCache();

  struct MockLoadDnsCacheResult {
    LoadDnsCacheStatus status_;
    LoadDnsCacheHandle* handle_;
  };

  LoadDnsCacheResult loadDnsCache(absl::string_view host, uint16_t default_port,
                                  LoadDnsCacheCallbacks& callbacks) override {
    MockLoadDnsCacheResult result = loadDnsCache_(host, default_port, callbacks);
    return {result.status_, LoadDnsCacheHandlePtr{result.handle_}};
  }
  MOCK_METHOD3(loadDnsCache_, MockLoadDnsCacheResult(absl::string_view host, uint16_t default_port,
                                                     LoadDnsCacheCallbacks& callbacks));

  AddUpdateCallbacksHandlePtr addUpdateCallbacks(UpdateCallbacks& callbacks) override {
    return AddUpdateCallbacksHandlePtr{addUpdateCallbacks_(callbacks)};
  }
  MOCK_METHOD1(addUpdateCallbacks_,
               DnsCache::AddUpdateCallbacksHandle*(UpdateCallbacks& callbacks));
};

class MockLoadDnsCacheHandle : public DnsCache::LoadDnsCacheHandle {
public:
  MockLoadDnsCacheHandle();
  ~MockLoadDnsCacheHandle();

  MOCK_METHOD0(onDestroy, void());
};

class MockDnsCacheManager : public DnsCacheManager {
public:
  MockDnsCacheManager();
  ~MockDnsCacheManager();

  MOCK_METHOD1(
      getCache,
      DnsCacheSharedPtr(
          const envoy::config::common::dynamic_forward_proxy::v2alpha::DnsCacheConfig& config));

  std::shared_ptr<MockDnsCache> dns_cache_{new MockDnsCache()};
};

class MockDnsHostInfo : public DnsHostInfo {
public:
  MockDnsHostInfo();
  ~MockDnsHostInfo();

  MOCK_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_METHOD0(touch, void());

  Network::Address::InstanceConstSharedPtr address_;
};

class MockUpdateCallbacks : public DnsCache::UpdateCallbacks {
public:
  MockUpdateCallbacks();
  ~MockUpdateCallbacks();

  MOCK_METHOD2(onDnsHostAddOrUpdate,
               void(const std::string& host, const DnsHostInfoSharedPtr& address));
  MOCK_METHOD1(onDnsHostRemove, void(const std::string& host));
};

class MockLoadDnsCacheCallbacks : public DnsCache::LoadDnsCacheCallbacks {
public:
  MockLoadDnsCacheCallbacks();
  ~MockLoadDnsCacheCallbacks();

  MOCK_METHOD0(onLoadDnsCacheComplete, void());
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
