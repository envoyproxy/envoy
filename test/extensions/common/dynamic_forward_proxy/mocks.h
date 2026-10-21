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

  MOCK_METHOD(ResourceLimit&, pendingRequests, (), (override));
  MOCK_METHOD(DnsCacheCircuitBreakersStats&, stats, (), (override));

  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;
};

class MockDnsCache : public DnsCache {
public:
  MockDnsCache();
  ~MockDnsCache() override;

  struct MockLoadDnsCacheEntryResult {
    LoadDnsCacheEntryStatus status_;
    LoadDnsCacheEntryHandle* handle_;
    std::optional<DnsHostInfoSharedPtr> host_info_;
  };

  LoadDnsCacheEntryResult
  loadDnsCacheEntryWithForceRefresh(absl::string_view host, uint16_t default_port,
                                    bool is_proxy_request, bool,
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

  MOCK_METHOD((void), iterateHostMap, (IterateHostMapCb), (override));
  MOCK_METHOD((std::optional<const DnsHostInfoSharedPtr>), getHost, (absl::string_view),
              (override));
  MOCK_METHOD(Upstream::ResourceAutoIncDec*, canCreateDnsRequest_, ());
  MOCK_METHOD(void, forceRefreshHosts, (), (override));
  MOCK_METHOD(void, setIpVersionToRemove, (std::optional<Network::Address::IpVersion>), (override));
  MOCK_METHOD(std::optional<Network::Address::IpVersion>, getIpVersionToRemove, (), (override));
  MOCK_METHOD(void, stop, (), (override));
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
              (const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config),
              (override));
  MOCK_METHOD(DnsCacheSharedPtr, lookUpCacheByName, (absl::string_view cache_name), (override));

  std::shared_ptr<NiceMock<MockDnsCache>> dns_cache_{new NiceMock<MockDnsCache>()};
};

class MockDnsHostInfo : public DnsHostInfo {
public:
  MockDnsHostInfo();
  ~MockDnsHostInfo() override;

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const, override));
  MOCK_METHOD(std::vector<Network::Address::InstanceConstSharedPtr>, addressList, (),
              (const, override));
  MOCK_METHOD(const std::string&, resolvedHost, (), (const, override));
  MOCK_METHOD(bool, isIpAddress, (), (const, override));
  MOCK_METHOD(void, touch, (), (override));
  MOCK_METHOD(bool, firstResolveComplete, (), (const, override));
  MOCK_METHOD(std::string, details, (), (override));
  MOCK_METHOD(Network::DnsResolver::ResolutionStatus, resolutionStatus, (), (const, override));

  Network::Address::InstanceConstSharedPtr address_;
  std::vector<Network::Address::InstanceConstSharedPtr> address_list_;
  std::string resolved_host_;
};

class MockUpdateCallbacks : public DnsCache::UpdateCallbacks {
public:
  MockUpdateCallbacks();
  ~MockUpdateCallbacks() override;

  MOCK_METHOD(absl::Status, onDnsHostAddOrUpdate,
              (const std::string& host, const DnsHostInfoSharedPtr& address), (override));
  MOCK_METHOD(void, onDnsHostRemove, (const std::string& host), (override));
  MOCK_METHOD(void, onDnsResolutionComplete,
              (const std::string& host, const DnsHostInfoSharedPtr& host_info,
               Network::DnsResolver::ResolutionStatus status),
              (override));
};

class MockLoadDnsCacheEntryCallbacks : public DnsCache::LoadDnsCacheEntryCallbacks {
public:
  MockLoadDnsCacheEntryCallbacks();
  ~MockLoadDnsCacheEntryCallbacks() override;

  MOCK_METHOD(void, onLoadDnsCacheComplete, (const DnsHostInfoSharedPtr&), (override));
};

class MockDfpCluster : public DfpCluster {
public:
  MockDfpCluster() = default;
  ~MockDfpCluster() override = default;

  // Extensions::Common::DynamicForwardProxy::DfpCluster
  MOCK_METHOD(bool, enableSubCluster, (), (const, override));
  MOCK_METHOD((std::pair<bool, std::optional<envoy::config::cluster::v3::Cluster>>),
              createSubClusterConfig, (const std::string&, const std::string&, const int),
              (override));
  MOCK_METHOD(bool, touch, (const std::string&), (override));
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
