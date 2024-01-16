#pragma once

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

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
    absl::optional<DnsHostInfoSharedPtr> host_info_;
  };

  LoadDnsCacheEntryResult loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                            bool is_proxy_request,
                                            LoadDnsCacheEntryCallbacks& callbacks) override {
    MockLoadDnsCacheEntryResult result =
        loadDnsCacheEntry_(host, default_port, is_proxy_request, callbacks);
    return {result.status_, LoadDnsCacheEntryHandlePtr{result.handle_}, result.host_info_};
  }
  Upstream::ResourceAutoIncDecPtr canCreateDnsRequest() override {
    Upstream::ResourceAutoIncDec* raii_ptr = canCreateDnsRequest_();
    return std::unique_ptr<Upstream::ResourceAutoIncDec>(raii_ptr);
  }
  MOCK_METHOD(MockLoadDnsCacheEntryResult, loadDnsCacheEntry_,
              (absl::string_view host, uint16_t default_port, bool is_proxy_request,
               LoadDnsCacheEntryCallbacks& callbacks));

  AddUpdateCallbacksHandlePtr addUpdateCallbacks(UpdateCallbacks& callbacks) override {
    return AddUpdateCallbacksHandlePtr{addUpdateCallbacks_(callbacks)};
  }
  MOCK_METHOD(DnsCache::AddUpdateCallbacksHandle*, addUpdateCallbacks_,
              (UpdateCallbacks & callbacks));

  MOCK_METHOD((void), iterateHostMap, (IterateHostMapCb));
  MOCK_METHOD((absl::optional<const DnsHostInfoSharedPtr>), getHost, (absl::string_view));
  MOCK_METHOD(Upstream::ResourceAutoIncDec*, canCreateDnsRequest_, ());
  MOCK_METHOD(void, forceRefreshHosts, ());
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

  MOCK_METHOD(absl::StatusOr<DnsCacheSharedPtr>, getCache,
              (const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config));
  MOCK_METHOD(DnsCacheSharedPtr, lookUpCacheByName, (absl::string_view cache_name));

  std::shared_ptr<NiceMock<MockDnsCache>> dns_cache_{new NiceMock<MockDnsCache>()};
};

class MockDnsHostInfo : public DnsHostInfo {
public:
  MockDnsHostInfo();
  ~MockDnsHostInfo() override;

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const));
  MOCK_METHOD(std::vector<Network::Address::InstanceConstSharedPtr>, addressList, (), (const));
  MOCK_METHOD(const std::string&, resolvedHost, (), (const));
  MOCK_METHOD(bool, isIpAddress, (), (const));
  MOCK_METHOD(void, touch, ());
  MOCK_METHOD(bool, firstResolveComplete, (), (const));

  Network::Address::InstanceConstSharedPtr address_;
  std::vector<Network::Address::InstanceConstSharedPtr> address_list_;
  std::string resolved_host_;
};

class MockUpdateCallbacks : public DnsCache::UpdateCallbacks {
public:
  MockUpdateCallbacks();
  ~MockUpdateCallbacks() override;

  MOCK_METHOD(void, onDnsHostAddOrUpdate,
              (const std::string& host, const DnsHostInfoSharedPtr& address));
  MOCK_METHOD(void, onDnsHostRemove, (const std::string& host));
  MOCK_METHOD(void, onDnsResolutionComplete,
              (const std::string& host, const DnsHostInfoSharedPtr& host_info,
               Network::DnsResolver::ResolutionStatus status));
};

class MockLoadDnsCacheEntryCallbacks : public DnsCache::LoadDnsCacheEntryCallbacks {
public:
  MockLoadDnsCacheEntryCallbacks();
  ~MockLoadDnsCacheEntryCallbacks() override;

  MOCK_METHOD(void, onLoadDnsCacheComplete, (const DnsHostInfoSharedPtr&));
};

class MockDfpCluster : public DfpCluster {
public:
  MockDfpCluster() = default;
  ~MockDfpCluster() override = default;

  // Extensions::Common::DynamicForwardProxy::DfpCluster
  MOCK_METHOD(bool, enableSubCluster, (), (const));
  MOCK_METHOD((std::pair<bool, absl::optional<envoy::config::cluster::v3::Cluster>>),
              createSubClusterConfig, (const std::string&, const std::string&, const int));
  MOCK_METHOD(bool, touch, (const std::string&));
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
