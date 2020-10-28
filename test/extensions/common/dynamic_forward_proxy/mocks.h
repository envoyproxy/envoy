#pragma once

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "test/mocks/upstream/basic_resource_limit.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class MockDnsCacheResourceManager : public DnsCacheResourceManager {
public:
  MockDnsCacheResourceManager();
  ~MockDnsCacheResourceManager() override;

  MOCK_METHOD(ResourceLimit&, pendingRequests, ());
  MOCK_METHOD(DnsCacheCircuitBreakersStats&, stats, ());

  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;
};

class MockDnsCache : public DnsCache {
public:
  MockDnsCache();
  ~MockDnsCache() override;

  struct MockLoadDnsCacheEntryResult {
    LoadDnsCacheEntryStatus status_;
    LoadDnsCacheEntryHandle* handle_;
  };

  LoadDnsCacheEntryResult loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                            LoadDnsCacheEntryCallbacks& callbacks) override {
    MockLoadDnsCacheEntryResult result = loadDnsCacheEntry_(host, default_port, callbacks);
    return {result.status_, LoadDnsCacheEntryHandlePtr{result.handle_}};
  }
  Upstream::ResourceAutoIncDecPtr
  canCreateDnsRequest(ResourceLimitOptRef pending_requests) override {
    Upstream::ResourceAutoIncDec* raii_ptr = canCreateDnsRequest_(pending_requests);
    return std::unique_ptr<Upstream::ResourceAutoIncDec>(raii_ptr);
  }
  MOCK_METHOD(MockLoadDnsCacheEntryResult, loadDnsCacheEntry_,
              (absl::string_view host, uint16_t default_port,
               LoadDnsCacheEntryCallbacks& callbacks));

  AddUpdateCallbacksHandlePtr addUpdateCallbacks(UpdateCallbacks& callbacks) override {
    return AddUpdateCallbacksHandlePtr{addUpdateCallbacks_(callbacks)};
  }
  MOCK_METHOD(DnsCache::AddUpdateCallbacksHandle*, addUpdateCallbacks_,
              (UpdateCallbacks & callbacks));

  MOCK_METHOD((absl::flat_hash_map<std::string, DnsHostInfoSharedPtr>), hosts, ());
  MOCK_METHOD((absl::optional<const DnsHostInfoSharedPtr>), getHost, (absl::string_view));
  MOCK_METHOD(Upstream::ResourceAutoIncDec*, canCreateDnsRequest_, (ResourceLimitOptRef));
};

class MockLoadDnsCacheEntryHandle : public DnsCache::LoadDnsCacheEntryHandle {
public:
  MockLoadDnsCacheEntryHandle();
  ~MockLoadDnsCacheEntryHandle() override;

  MOCK_METHOD(void, onDestroy, ());
};

class MockDnsCacheManager : public DnsCacheManager {
public:
  MockDnsCacheManager();
  ~MockDnsCacheManager() override;

  MOCK_METHOD(DnsCacheSharedPtr, getCache,
              (const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config));

  std::shared_ptr<NiceMock<MockDnsCache>> dns_cache_{new NiceMock<MockDnsCache>()};
};

class MockDnsHostInfo : public DnsHostInfo {
public:
  MockDnsHostInfo();
  ~MockDnsHostInfo() override;

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, ());
  MOCK_METHOD(const std::string&, resolvedHost, (), (const));
  MOCK_METHOD(bool, isIpAddress, (), (const));
  MOCK_METHOD(void, touch, ());

  Network::Address::InstanceConstSharedPtr address_;
  std::string resolved_host_;
};

class MockUpdateCallbacks : public DnsCache::UpdateCallbacks {
public:
  MockUpdateCallbacks();
  ~MockUpdateCallbacks() override;

  MOCK_METHOD(void, onDnsHostAddOrUpdate,
              (const std::string& host, const DnsHostInfoSharedPtr& address));
  MOCK_METHOD(void, onDnsHostRemove, (const std::string& host));
};

class MockLoadDnsCacheEntryCallbacks : public DnsCache::LoadDnsCacheEntryCallbacks {
public:
  MockLoadDnsCacheEntryCallbacks();
  ~MockLoadDnsCacheEntryCallbacks() override;

  MOCK_METHOD(void, onLoadDnsCacheComplete, ());
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
