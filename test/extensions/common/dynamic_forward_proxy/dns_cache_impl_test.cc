#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/resolver.pb.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/network/resolver_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/server/factory_context_base_impl.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"

using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {
namespace {

static const absl::optional<std::chrono::seconds> kNoTtl = absl::nullopt;

class DnsCacheImplTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  DnsCacheImplTest() : registered_dns_factory_(dns_resolver_factory_) {}
  void initialize(
      std::vector<std::pair<std::string /*host*/, uint32_t /*port*/>> preresolve_hostnames = {},
      uint32_t max_hosts = 1024) {
    config_.set_name("foo");
    config_.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);
    config_.mutable_max_hosts()->set_value(max_hosts);
    if (!preresolve_hostnames.empty()) {
      for (const auto& [host, port] : preresolve_hostnames) {
        envoy::config::core::v3::SocketAddress* address = config_.add_preresolve_hostnames();
        address->set_address(host);
        address->set_port_value(port);
      }
    }

    EXPECT_CALL(context_.server_factory_context_.dispatcher_, isThreadSafe)
        .WillRepeatedly(Return(true));

    EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
        .WillRepeatedly(Return(resolver_));
    auto status_or_cache = DnsCacheImpl::createDnsCacheImpl(context_, config_);
    THROW_IF_STATUS_NOT_OK(status_or_cache, throw);
    dns_cache_ = status_or_cache.value();
    update_callbacks_handle_ = dns_cache_->addUpdateCallbacks(update_callbacks_);
  }

  ~DnsCacheImplTest() override {
    if (dns_cache_.get()) {
      dns_cache_.reset();
      EXPECT_EQ(0, TestUtility::findGauge(context_.store_, "dns_cache.foo.num_hosts")->value());
    }
  }

  void checkStats(uint64_t query_attempt, uint64_t query_success, uint64_t query_failure,
                  uint64_t address_changed, uint64_t added, uint64_t removed, uint64_t num_hosts) {
    const auto counter_value = [this](const std::string& name) {
      return TestUtility::findCounter(context_.store_, "dns_cache.foo." + name)->value();
    };

    EXPECT_EQ(query_attempt, counter_value("dns_query_attempt"));
    EXPECT_EQ(query_success, counter_value("dns_query_success"));
    EXPECT_EQ(query_failure, counter_value("dns_query_failure"));
    EXPECT_EQ(address_changed, counter_value("host_address_changed"));
    EXPECT_EQ(added, counter_value("host_added"));
    EXPECT_EQ(removed, counter_value("host_removed"));
    EXPECT_EQ(num_hosts,
              TestUtility::findGauge(context_.store_, "dns_cache.foo.num_hosts")->value());
  }

  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  std::shared_ptr<DnsCache> dns_cache_;
  MockUpdateCallbacks update_callbacks_;
  DnsCache::AddUpdateCallbacksHandlePtr update_callbacks_handle_;
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;
  std::chrono::milliseconds configured_ttl_ = std::chrono::milliseconds(60000);
  std::chrono::milliseconds dns_ttl_ = std::chrono::milliseconds(6000);
};

MATCHER_P3(DnsHostInfoEquals, address, resolved_host, is_ip_address, "") {
  bool equal = address == arg->address()->asString();
  if (!equal) {
    *result_listener << fmt::format("address '{}' != '{}'", address, arg->address()->asString());
    return equal;
  }
  equal &= resolved_host == arg->resolvedHost();
  if (!equal) {
    *result_listener << fmt::format("resolved_host '{}' != '{}'", resolved_host,
                                    arg->resolvedHost());
    return equal;
  }
  equal &= is_ip_address == arg->isIpAddress();
  if (!equal) {
    *result_listener << fmt::format("is_ip_address '{}' != '{}'", is_ip_address,
                                    arg->isIpAddress());
  }
  return equal;
}

MATCHER(DnsHostInfoAddressIsNull, "") { return arg->address() == nullptr; }
MATCHER(DnsHostInfoFirstResolveCompleteTrue, "") { return arg->firstResolveComplete() == false; }

void verifyCaresDnsConfigAndUnpack(
    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config,
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig& cares) {
  // Verify typed DNS resolver config is c-ares.
  EXPECT_EQ(typed_dns_resolver_config.name(), std::string(Network::CaresDnsResolver));
  EXPECT_EQ(
      typed_dns_resolver_config.typed_config().type_url(),
      "type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig");
  typed_dns_resolver_config.typed_config().UnpackTo(&cares);
}

class DnsCacheImplPreresolveTest : public DnsCacheImplTest,
                                   public testing::WithParamInterface<bool> {
public:
  bool normalizeDfpHost() { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(DnsCachePreresolveNormalizedDfpHost, DnsCacheImplPreresolveTest,
                         testing::Bool());

TEST_P(DnsCacheImplPreresolveTest, PreresolveSuccess) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.normalize_host_for_preresolve_dfp_dns",
                               absl::StrCat(normalizeDfpHost())}});

  Network::DnsResolver::ResolveCb resolve_cb;
  std::string host = "bar.baz.com";
  uint32_t port = 443;
  std::string authority = absl::StrCat(host, ":", port);
  EXPECT_CALL(*resolver_, resolve(host, _, _))
      .WillRepeatedly(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate(authority, DnsHostInfoEquals("10.0.0.1:443", "bar.baz.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete(authority,
                                      DnsHostInfoEquals("10.0.0.1:443", "bar.baz.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));

  initialize({{normalizeDfpHost() ? host : authority, port}} /* preresolve_hostnames */);

  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  MockLoadDnsCacheEntryCallbacks callbacks;
  if (normalizeDfpHost()) {
    // Retrieve with the hostname and port in the "host".
    auto result = dns_cache_->loadDnsCacheEntry(authority, port, false, callbacks);
    EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
    EXPECT_EQ(result.handle_, nullptr);
    EXPECT_NE(absl::nullopt, result.host_info_);
  }
  // Retrieve with the hostname only in the "host".
  auto result = dns_cache_->loadDnsCacheEntry(host, port, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  EXPECT_NE(absl::nullopt, result.host_info_);
}

TEST_P(DnsCacheImplPreresolveTest, PreresolveFailure) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize({{"bar.baz.com", 443}} /* preresolve_hostnames */, 0 /* max_hosts */),
      EnvoyException,
      "DNS Cache [foo] configured with preresolve_hostnames=1 larger than max_hosts=0");
}

TEST_F(DnsCacheImplTest, DnsFirstResolveComplete) {
  // This test relies on below runtime flag to be true.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dns_cache_set_first_resolve_complete", "true"}});
  Network::DnsResolver::ResolveCb resolve_cb;
  std::string hostname = "bar.baz.com:443";
  EXPECT_CALL(*resolver_, resolve("bar.baz.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, DnsHostInfoFirstResolveCompleteTrue()));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("bar.baz.com:443",
                                      DnsHostInfoEquals("10.0.0.1:443", "bar.baz.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));

  initialize({{"bar.baz.com", 443}});
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  MockLoadDnsCacheEntryCallbacks callbacks;
  auto result = dns_cache_->loadDnsCacheEntry("bar.baz.com", 443, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  EXPECT_NE(absl::nullopt, result.host_info_);
}

// Basic successful resolution and then re-resolution.
TEST_F(DnsCacheImplTest, ResolveSuccess) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve timer.
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  checkStats(2 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Address does not change.
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve timer.
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  checkStats(3 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Address does change.
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.2:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.2:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.2"}));

  checkStats(3 /* attempt */, 3 /* success */, 0 /* failure */, 2 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);
}

// Verify the force refresh API works as expected.
TEST_F(DnsCacheImplTest, ForceRefresh) {
  initialize();
  InSequence s;

  // No hosts so should not do anything other than reset the resolver.
  EXPECT_CALL(*resolver_, resetNetworking());
  dns_cache_->forceRefreshHosts();
  checkStats(0 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             0 /* added */, 0 /* removed */, 0 /* num hosts */);

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Query in progress so should reset and then cancel.
  EXPECT_CALL(*resolver_, resetNetworking());
  EXPECT_CALL(resolver_->active_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(*timeout_timer, enabled()).Times(AtLeast(0));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(0), _));
  dns_cache_->forceRefreshHosts();
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);
}

// Ipv4 address.
TEST_F(DnsCacheImplTest, Ipv4Address) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("127.0.0.1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("127.0.0.1", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("127.0.0.1:80", DnsHostInfoEquals("127.0.0.1:80", "127.0.0.1", true)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("127.0.0.1:80", "127.0.0.1", true)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("127.0.0.1:80",
                                      DnsHostInfoEquals("127.0.0.1:80", "127.0.0.1", true),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"127.0.0.1"}));
}

// Ipv4 address with port.
TEST_F(DnsCacheImplTest, Ipv4AddressWithPort) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("127.0.0.1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("127.0.0.1:10000", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("127.0.0.1:10000",
                                   DnsHostInfoEquals("127.0.0.1:10000", "127.0.0.1", true)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("127.0.0.1:10000", "127.0.0.1", true)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("127.0.0.1:10000",
                                      DnsHostInfoEquals("127.0.0.1:10000", "127.0.0.1", true),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"127.0.0.1"}));
}

// Ipv6 address.
TEST_F(DnsCacheImplTest, Ipv6Address) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("::1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("[::1]", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("[::1]:80", DnsHostInfoEquals("[::1]:80", "::1", true)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoEquals("[::1]:80", "::1", true)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("[::1]:80", DnsHostInfoEquals("[::1]:80", "::1", true),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"::1"}));
}

// Ipv6 address with port.
TEST_F(DnsCacheImplTest, Ipv6AddressWithPort) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("::1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("[::1]:10000", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("[::1]:10000", DnsHostInfoEquals("[::1]:10000", "::1", true)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoEquals("[::1]:10000", "::1", true)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("[::1]:10000", DnsHostInfoEquals("[::1]:10000", "::1", true),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"::1"}));
}

// TTL purge test.
TEST_F(DnsCacheImplTest, TTL) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(6000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve with ~6s passed. The resolved entry TTL is 6s.
  simTime().advanceTimeWait(std::chrono::milliseconds(6001));
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();
  checkStats(2 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(6000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve with ~1m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::seconds(60000));
  EXPECT_CALL(update_callbacks_, onDnsHostRemove("foo.com:80"));
  resolve_timer->invokeCallback();
  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 1 /* removed */, 0 /* num hosts */);

  // Make sure we don't get a cache hit the next time the host is requested.
  new Event::MockTimer(&context_.server_factory_context_.dispatcher_); // resolve_timer
  timeout_timer = new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);
  checkStats(3 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             2 /* added */, 1 /* removed */, 1 /* num hosts */);
}

// Verify that dns_min_refresh_rate is honored.
TEST_F(DnsCacheImplTest, TTLWithMinRefreshRate) {
  *config_.mutable_dns_min_refresh_rate() = Protobuf::util::TimeUtil::SecondsToDuration(45);
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(45000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
}

// TTL purge test with different refresh/TTL parameters.
TEST_F(DnsCacheImplTest, TTLWithCustomParameters) {
  *config_.mutable_dns_refresh_rate() = Protobuf::util::TimeUtil::SecondsToDuration(30);
  *config_.mutable_host_ttl() = Protobuf::util::TimeUtil::SecondsToDuration(60);
  *config_.mutable_dns_query_timeout() = Protobuf::util::TimeUtil::SecondsToDuration(1);
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  // Re-resolve with ~30s passed. TTL should still be OK at 60s.
  simTime().advanceTimeWait(std::chrono::milliseconds(30001));
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(1000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  // Re-resolve with ~30s passed. TTL should expire.
  simTime().advanceTimeWait(std::chrono::milliseconds(30001));
  EXPECT_CALL(update_callbacks_, onDnsHostRemove("foo.com:80"));
  resolve_timer->invokeCallback();
}

// Resolve that completes inline without any callback.
TEST_F(DnsCacheImplTest, InlineResolve) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Event::PostCb post_cb;
  EXPECT_CALL(context_.server_factory_context_.dispatcher_, post(_))
      .WillOnce([&post_cb](Event::PostCb cb) { post_cb = std::move(cb); });
  auto result = dns_cache_->loadDnsCacheEntry("localhost", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("localhost", _, _))
      .WillOnce(Invoke([](const std::string&, Network::DnsLookupFamily,
                          Network::DnsResolver::ResolveCb callback) {
        callback(Network::DnsResolver::ResolutionStatus::Success,
                 TestUtility::makeDnsResponse({"127.0.0.1"}));
        return nullptr;
      }));
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("localhost:80", DnsHostInfoEquals("127.0.0.1:80", "localhost", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("127.0.0.1:80", "localhost", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("localhost:80",
                                      DnsHostInfoEquals("127.0.0.1:80", "localhost", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  post_cb();
}

// Resolve timeout.
TEST_F(DnsCacheImplTest, ResolveTimeout) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(resolver_->active_query_, cancel(Network::ActiveDnsQuery::CancelReason::Timeout));
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoAddressIsNull()));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80", DnsHostInfoAddressIsNull(),
                                      Network::DnsResolver::ResolutionStatus::Failure));
  // The resolve timeout will be the default TTL as there was no specific TTL
  // overriding.
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(configured_ttl_), _));
  timeout_timer->invokeCallback();
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);
  EXPECT_EQ(1,
            TestUtility::findCounter(context_.store_, "dns_cache.foo.dns_query_timeout")->value());
}

// Resolve failure that returns no addresses.
TEST_F(DnsCacheImplTest, ResolveFailure) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoAddressIsNull()));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80", DnsHostInfoAddressIsNull(),
                                      Network::DnsResolver::ResolutionStatus::Failure));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(configured_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Failure, TestUtility::makeDnsResponse({}));
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  ASSERT_NE(absl::nullopt, result.host_info_);
  ASSERT_NE(nullptr, *result.host_info_);
  EXPECT_EQ(nullptr, (*result.host_info_)->address());

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(600001));
  // Because resolution failed for the host, onDnsHostAddOrUpdate was not called.
  // Therefore, onDnsHostRemove should not be called either.
  EXPECT_CALL(update_callbacks_, onDnsHostRemove(_)).Times(0);
  resolve_timer->invokeCallback();
  // DnsCacheImpl state is updated accordingly: the host is removed.
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 1 /* removed */, 0 /* num hosts */);
}

TEST_F(DnsCacheImplTest, ResolveFailureWithFailureRefreshRate) {
  *config_.mutable_dns_failure_refresh_rate()->mutable_base_interval() =
      Protobuf::util::TimeUtil::SecondsToDuration(7);
  *config_.mutable_dns_failure_refresh_rate()->mutable_max_interval() =
      Protobuf::util::TimeUtil::SecondsToDuration(10);
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoAddressIsNull()));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80", DnsHostInfoAddressIsNull(),
                                      Network::DnsResolver::ResolutionStatus::Failure));
  ON_CALL(context_.server_factory_context_.api_.random_, random()).WillByDefault(Return(8000));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(1000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Failure, TestUtility::makeDnsResponse({}));
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  ASSERT_NE(absl::nullopt, result.host_info_);
  ASSERT_NE(nullptr, *result.host_info_);
  EXPECT_EQ(nullptr, (*result.host_info_)->address());

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(600001));
  // Because resolution failed for the host, onDnsHostAddOrUpdate was not called.
  // Therefore, onDnsHostRemove should not be called either.
  EXPECT_CALL(update_callbacks_, onDnsHostRemove(_)).Times(0);
  resolve_timer->invokeCallback();
  // DnsCacheImpl state is updated accordingly: the host is removed.
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 1 /* removed */, 0 /* num hosts */);
}

TEST_F(DnsCacheImplTest, ResolveSuccessWithEmptyResult) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // A successful empty resolution DOES NOT update the host information.
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoAddressIsNull()));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80", DnsHostInfoAddressIsNull(),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(configured_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));
  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  ASSERT_NE(absl::nullopt, result.host_info_);
  ASSERT_NE(nullptr, *result.host_info_);
  EXPECT_EQ(nullptr, (*result.host_info_)->address());

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(600001));
  // Because resolution failed for the host, onDnsHostAddOrUpdate was not called.
  // Therefore, onDnsHostRemove should not be called either.
  EXPECT_CALL(update_callbacks_, onDnsHostRemove(_)).Times(0);
  resolve_timer->invokeCallback();
  // DnsCacheImpl state is updated accordingly: the host is removed.
  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 1 /* removed */, 0 /* num hosts */);
}

// Cancel a cache load before the resolve completes.
TEST_F(DnsCacheImplTest, CancelResolve) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  result.handle_.reset();
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
}

// Two cache loads that are trying to resolve the same host. Make sure we only do a single resolve
// and fire both callbacks on completion.
TEST_F(DnsCacheImplTest, MultipleResolveSameHost) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks1;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result1 = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks1);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result1.status_);
  EXPECT_NE(result1.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result1.host_info_);

  MockLoadDnsCacheEntryCallbacks callbacks2;
  auto result2 = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks2);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result2.status_);
  EXPECT_NE(result2.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result2.host_info_);

  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks2,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks1,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
}

// Two cache loads that are resolving different hosts.
TEST_F(DnsCacheImplTest, MultipleResolveDifferentHost) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks1;
  Network::DnsResolver::ResolveCb resolve_cb1;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb1), Return(&resolver_->active_query_)));
  auto result1 = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks1);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result1.status_);
  EXPECT_NE(result1.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result1.host_info_);
  EXPECT_EQ(dns_cache_->getHost("foo.com"), absl::nullopt);

  MockLoadDnsCacheEntryCallbacks callbacks2;
  Network::DnsResolver::ResolveCb resolve_cb2;
  EXPECT_CALL(*resolver_, resolve("bar.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb2), Return(&resolver_->active_query_)));
  auto result2 = dns_cache_->loadDnsCacheEntry("bar.com", 443, false, callbacks2);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result2.status_);
  EXPECT_NE(result2.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result2.host_info_);
  EXPECT_EQ(dns_cache_->getHost("bar.com"), absl::nullopt);

  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("bar.com:443", DnsHostInfoEquals("10.0.0.1:443", "bar.com", false)));
  EXPECT_CALL(callbacks2,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:443", "bar.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("bar.com:443",
                                      DnsHostInfoEquals("10.0.0.1:443", "bar.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb2(Network::DnsResolver::ResolutionStatus::Success,
              TestUtility::makeDnsResponse({"10.0.0.1"}));

  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.2:80", "foo.com", false)));
  EXPECT_CALL(callbacks1,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.2:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.2:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb1(Network::DnsResolver::ResolutionStatus::Success,
              TestUtility::makeDnsResponse({"10.0.0.2"}));

  absl::flat_hash_map<std::string, DnsHostInfoSharedPtr> hosts;
  dns_cache_->iterateHostMap(
      [&](absl::string_view host, const DnsHostInfoSharedPtr& info) { hosts.emplace(host, info); });
  EXPECT_EQ(2, hosts.size());
  EXPECT_THAT(hosts["bar.com:443"], DnsHostInfoEquals("10.0.0.1:443", "bar.com", false));
  EXPECT_THAT(hosts["foo.com:80"], DnsHostInfoEquals("10.0.0.2:80", "foo.com", false));

  EXPECT_TRUE(dns_cache_->getHost("bar.com:443").has_value());
  EXPECT_THAT(dns_cache_->getHost("bar.com:443").value(),
              DnsHostInfoEquals("10.0.0.1:443", "bar.com", false));
  EXPECT_TRUE(dns_cache_->getHost("foo.com:80").has_value());
  EXPECT_THAT(dns_cache_->getHost("foo.com:80").value(),
              DnsHostInfoEquals("10.0.0.2:80", "foo.com", false));
  EXPECT_EQ(dns_cache_->getHost("baz.com"), absl::nullopt);
}

// A successful resolve followed by a cache hit.
TEST_F(DnsCacheImplTest, CacheHit) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  ASSERT_NE(absl::nullopt, result.host_info_);
  EXPECT_THAT(*result.host_info_, DnsHostInfoEquals("10.0.0.1:80", "foo.com", false));
}

// A successful resolve followed by a cache hit with different default port.
TEST_F(DnsCacheImplTest, CacheHitWithDifferentDefaultPort) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  // Using the same DNS cache, resolve the same host but different default port 443
  // This currently will re-resolve as we don't have an optimization to look up
  // cache entries with different ports.
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  result = dns_cache_->loadDnsCacheEntry("foo.com", 443, false, callbacks);
}

// Make sure we destroy active queries if the cache goes away.
TEST_F(DnsCacheImplTest, CancelActiveQueriesOnDestroy) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(resolver_->active_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  dns_cache_.reset();
}

// Invalid port
TEST_F(DnsCacheImplTest, InvalidPort) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com:abc", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com:abc", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete(DnsHostInfoAddressIsNull()));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:abc:80", DnsHostInfoAddressIsNull(),
                                      Network::DnsResolver::ResolutionStatus::Success));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));
}

// Max host overflow.
TEST_F(DnsCacheImplTest, MaxHostOverflow) {
  initialize({} /* preresolve_hostnames */, 0 /* max_hosts */);
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Overflow, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);
  EXPECT_EQ(1, TestUtility::findCounter(context_.store_, "dns_cache.foo.host_overflow")->value());
}

TEST_F(DnsCacheImplTest, CircuitBreakersNotInvoked) {
  initialize();

  auto raii_ptr = dns_cache_->canCreateDnsRequest();
  EXPECT_NE(raii_ptr.get(), nullptr);
}

TEST_F(DnsCacheImplTest, DnsCacheCircuitBreakersOverflow) {
  config_.mutable_dns_cache_circuit_breaker()->mutable_max_pending_requests()->set_value(0);
  initialize();

  auto raii_ptr = dns_cache_->canCreateDnsRequest();
  EXPECT_EQ(raii_ptr.get(), nullptr);
  EXPECT_EQ(
      1,
      TestUtility::findCounter(context_.store_, "dns_cache.foo.dns_rq_pending_overflow")->value());
}

TEST_F(DnsCacheImplTest, UseTcpForDnsLookupsOptionSetDeprecatedField) {
  initialize();
  config_.set_use_tcp_for_dns_lookups(true);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(resolver_)));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context_, config_).value();
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  // `true` here means dns_resolver_options.use_tcp_for_dns_lookups is set to true.
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
}

TEST_F(DnsCacheImplTest, UseTcpForDnsLookupsOptionSet) {
  initialize();
  config_.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_use_tcp_for_dns_lookups(true);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(resolver_)));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context_, config_).value();
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  // `true` here means dns_resolver_options.use_tcp_for_dns_lookups is set to true.
  EXPECT_EQ(true, cares.dns_resolver_options().use_tcp_for_dns_lookups());
}

TEST_F(DnsCacheImplTest, NoDefaultSearchDomainOptionSet) {
  initialize();
  config_.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_no_default_search_domain(true);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(resolver_)));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context_, config_).value();
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  // `true` here means dns_resolver_options.no_default_search_domain is set to true.
  EXPECT_EQ(true, cares.dns_resolver_options().no_default_search_domain());
}

TEST_F(DnsCacheImplTest, UseTcpForDnsLookupsOptionUnSet) {
  initialize();
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(resolver_)));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context_, config_).value();
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  // `false` here means dns_resolver_options.use_tcp_for_dns_lookups is set to false.
  EXPECT_EQ(false, cares.dns_resolver_options().use_tcp_for_dns_lookups());
}

TEST_F(DnsCacheImplTest, NoDefaultSearchDomainOptionUnSet) {
  initialize();
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&typed_dns_resolver_config), Return(resolver_)));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context_, config_).value();
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  verifyCaresDnsConfigAndUnpack(typed_dns_resolver_config, cares);
  // `false` here means dns_resolver_options.no_default_search_domain is set to false.
  EXPECT_EQ(false, cares.dns_resolver_options().no_default_search_domain());
}

// DNS cache manager config tests.
TEST(DnsCacheManagerImplTest, LoadViaConfig) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  DnsCacheManagerImpl cache_manager(context);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config1;
  config1.set_name("foo");

  auto cache1 = cache_manager.getCache(config1).value();
  EXPECT_NE(cache1, nullptr);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config2;
  config2.set_name("foo");
  EXPECT_EQ(cache1, cache_manager.getCache(config2).value());

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config3;
  config3.set_name("bar");
  auto cache2 = cache_manager.getCache(config3).value();
  EXPECT_NE(cache2, nullptr);
  EXPECT_NE(cache1, cache2);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config4;
  config4.set_name("foo");
  config4.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V6_ONLY);
  EXPECT_EQ(cache_manager.getCache(config4).status().message(),
            "config specified DNS cache 'foo' with different settings");
}

TEST(DnsCacheManagerImplTest, LookupByName) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  DnsCacheManagerImpl cache_manager(context);

  EXPECT_EQ(cache_manager.lookUpCacheByName("foo"), nullptr);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config1;
  config1.set_name("foo");

  auto cache1 = cache_manager.getCache(config1).value();
  EXPECT_NE(cache1, nullptr);

  auto cache2 = cache_manager.lookUpCacheByName("foo");
  EXPECT_NE(cache2, nullptr);
  EXPECT_EQ(cache1, cache2);
}

TEST(DnsCacheConfigOptionsTest, EmtpyDnsResolutionConfig) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;
  std::shared_ptr<Network::MockDnsResolver> resolver{std::make_shared<Network::MockDnsResolver>()};
  envoy::config::core::v3::TypedExtensionConfig empty_typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  empty_typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  empty_typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory(dns_resolver_factory);
  EXPECT_CALL(dns_resolver_factory,
              createDnsResolver(_, _, ProtoEq(empty_typed_dns_resolver_config)))
      .WillOnce(Return(resolver));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context, config).value();
}

// Test dns_resolution_config is in place, use it.
TEST(DnsCacheConfigOptionsTest, NonEmptyDnsResolutionConfig) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;
  std::shared_ptr<Network::MockDnsResolver> resolver{std::make_shared<Network::MockDnsResolver>()};
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 80),
                                             resolvers);
  config.mutable_dns_resolution_config()->add_resolvers()->MergeFrom(resolvers);
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.add_resolvers()->MergeFrom(resolvers);
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory(dns_resolver_factory);
  EXPECT_CALL(dns_resolver_factory, createDnsResolver(_, _, ProtoEq(typed_dns_resolver_config)))
      .WillOnce(Return(resolver));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context, config).value();
}

// Test dns_resolution_config is in place, use it and overriding use_tcp_for_dns_lookups.
TEST(DnsCacheConfigOptionsTest, NonEmptyDnsResolutionConfigOverridingUseTcp) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  std::shared_ptr<Network::MockDnsResolver> resolver{std::make_shared<Network::MockDnsResolver>()};
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;

  // setup use_tcp
  config.set_use_tcp_for_dns_lookups(false);

  // setup dns_resolution_config
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 8080),
                                             resolvers);
  config.mutable_dns_resolution_config()->add_resolvers()->MergeFrom(resolvers);
  config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_use_tcp_for_dns_lookups(true);
  config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_no_default_search_domain(true);

  // setup expected typed config parameter
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.add_resolvers()->MergeFrom(resolvers);
  cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(true);
  cares.mutable_dns_resolver_options()->set_no_default_search_domain(true);
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));

  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory(dns_resolver_factory);
  EXPECT_CALL(dns_resolver_factory, createDnsResolver(_, _, ProtoEq(typed_dns_resolver_config)))
      .WillOnce(Return(resolver));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context, config).value();
}

// Test the case that the typed_dns_resolver_config is specified, and it overrides all
// other configuration, like config.dns_resolution_config, and config.use_tcp_for_dns_lookups.
TEST(DnsCacheConfigOptionsTest, NonEmptyTypedDnsResolverConfig) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  std::shared_ptr<Network::MockDnsResolver> resolver{std::make_shared<Network::MockDnsResolver>()};
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;

  // setup dns_resolution_config
  envoy::config::core::v3::Address resolvers;
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("1.2.3.4", 8080),
                                             resolvers);
  config.mutable_dns_resolution_config()->add_resolvers()->MergeFrom(resolvers);
  config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_use_tcp_for_dns_lookups(false);
  config.mutable_dns_resolution_config()
      ->mutable_dns_resolver_options()
      ->set_no_default_search_domain(false);

  // setup use_tcp_for_dns_lookups
  config.set_use_tcp_for_dns_lookups(false);

  // setup typed_dns_resolver_config
  Network::Utility::addressToProtobufAddress(Network::Address::Ipv4Instance("5.6.7.8", 9090),
                                             resolvers);
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  cares.add_resolvers()->MergeFrom(resolvers);
  cares.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(true);
  cares.mutable_dns_resolver_options()->set_no_default_search_domain(true);
  config.mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(cares);
  config.mutable_typed_dns_resolver_config()->set_name(std::string(Network::CaresDnsResolver));

  // setup the expected function call parameter.
  envoy::config::core::v3::TypedExtensionConfig expected_typed_dns_resolver_config;
  expected_typed_dns_resolver_config.mutable_typed_config()->PackFrom(cares);
  expected_typed_dns_resolver_config.set_name(std::string(Network::CaresDnsResolver));
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory(dns_resolver_factory);
  EXPECT_CALL(dns_resolver_factory,
              createDnsResolver(_, _, ProtoEq(expected_typed_dns_resolver_config)))
      .WillOnce(Return(resolver));
  std::shared_ptr<DnsCacheImpl> dns_cache =
      DnsCacheImpl::createDnsCacheImpl(context, config).value();
}

// Note: this test is done here, rather than a TYPED_TEST_SUITE in
// //test/common/config:utility_test, because we did not want to include an extension type in
// non-extension test suites.
// TODO(junr03): I ran into problems with templatizing this test and macro expansion.
// I spent too much time trying to figure this out. So for the moment I have copied this test body
// here. I will spend some more time fixing this, but wanted to land unblocking functionality first.
TEST(UtilityTest, PrepareDnsRefreshStrategy) {
  NiceMock<Random::MockRandomGenerator> random;

  {
    // dns_failure_refresh_rate not set.
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig dns_cache_config;
    BackOffStrategyPtr strategy = Config::Utility::prepareDnsRefreshStrategy<
        envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig>(dns_cache_config,
                                                                              5000, random);
    EXPECT_NE(nullptr, dynamic_cast<FixedBackOffStrategy*>(strategy.get()));
  }

  {
    // dns_failure_refresh_rate set.
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig dns_cache_config;
    dns_cache_config.mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(7);
    dns_cache_config.mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(10);
    BackOffStrategyPtr strategy = Config::Utility::prepareDnsRefreshStrategy<
        envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig>(dns_cache_config,
                                                                              5000, random);
    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
  }

  {
    // dns_failure_refresh_rate set with invalid max_interval.
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig dns_cache_config;
    dns_cache_config.mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(7);
    dns_cache_config.mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(2);
    EXPECT_THROW_WITH_REGEX(
        Config::Utility::prepareDnsRefreshStrategy<
            envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig>(dns_cache_config,
                                                                                  5000, random),
        EnvoyException,
        "dns_failure_refresh_rate must have max_interval greater than "
        "or equal to the base_interval");
  }
}

TEST_F(DnsCacheImplTest, ResolveSuccessWithCaching) {
  auto* time_source = new NiceMock<MockTimeSystem>();
  context_.server_factory_context_.dispatcher_.time_system_.reset(time_source);

  // Configure the cache.
  MockKeyValueStoreFactory factory;
  EXPECT_CALL(factory, createEmptyConfigProto()).WillRepeatedly(Invoke([]() {
    return std::make_unique<
        envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig>();
  }));
  MockKeyValueStore* store{};
  EXPECT_CALL(factory, createStore(_, _, _, _)).WillOnce(Invoke([this, &store]() {
    auto ret = std::make_unique<NiceMock<MockKeyValueStore>>();
    store = ret.get();
    // Make sure there's an attempt to load from the key value store.
    EXPECT_CALL(*store, iterate(_));
    // Make sure the result is sent to the worker threads.
    EXPECT_CALL(context_.server_factory_context_.thread_local_, runOnAllThreads(_)).Times(2);
    return ret;
  }));

  Registry::InjectFactory<KeyValueStoreFactory> injector(factory);
  auto* key_value_config = config_.mutable_key_value_config()->mutable_config();
  key_value_config->set_name("mock_key_value_store_factory");
  key_value_config->mutable_typed_config()->PackFrom(
      envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig());

  initialize();
  InSequence s;
  ASSERT(store != nullptr);

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  Event::MockTimer* timeout_timer =
      new Event::MockTimer(&context_.server_factory_context_.dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  EXPECT_EQ(absl::nullopt, result.host_info_);

  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  // Make sure the store gets the first insert.
  EXPECT_CALL(*store, addOrUpdate("foo.com:80", "10.0.0.1:80|6|0", kNoTtl));
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks,
              onLoadDnsCacheComplete(DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(6000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve timer.
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  checkStats(2 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Address does not change.
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(*store, addOrUpdate("foo.com:80", "10.0.0.1:80|6|0", kNoTtl));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.1:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve timer.
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  checkStats(3 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*timeout_timer, disableTimer());
  // Make sure the store gets the updated addresses
  EXPECT_CALL(*store, addOrUpdate("foo.com:80", "10.0.0.2:80|6|0\n10.0.0.1:80|6|0", kNoTtl));
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("foo.com:80", DnsHostInfoEquals("10.0.0.2:80", "foo.com", false)));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.2:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(dns_ttl_), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.2", "10.0.0.1"}));

  checkStats(3 /* attempt */, 3 /* success */, 0 /* failure */, 2 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Now do one more resolve, where the address does not change but the time
  // does.
  // Re-resolve timer.
  EXPECT_CALL(*timeout_timer, enableTimer(std::chrono::milliseconds(5000), nullptr));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  // Address does not change.
  EXPECT_CALL(*timeout_timer, disableTimer());
  EXPECT_CALL(*store, addOrUpdate("foo.com:80", "10.0.0.2:80|40|0\n10.0.0.1:80|40|0", kNoTtl));
  EXPECT_CALL(update_callbacks_,
              onDnsResolutionComplete("foo.com:80",
                                      DnsHostInfoEquals("10.0.0.2:80", "foo.com", false),
                                      Network::DnsResolver::ResolutionStatus::Success));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(40000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.2", "10.0.0.1"}, std::chrono::seconds(40)));
}

TEST_F(DnsCacheImplTest, CacheLoad) {
  auto* time_source = new NiceMock<MockTimeSystem>();
  context_.server_factory_context_.dispatcher_.time_system_.reset(time_source);

  // Configure the cache.
  MockKeyValueStoreFactory factory;
  EXPECT_CALL(factory, createEmptyConfigProto()).WillRepeatedly(Invoke([]() {
    return std::make_unique<
        envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig>();
  }));
  MockKeyValueStore* store{};
  EXPECT_CALL(factory, createStore(_, _, _, _)).WillOnce(Invoke([&store]() {
    auto ret = std::make_unique<NiceMock<MockKeyValueStore>>();
    store = ret.get();
    // Make sure there's an attempt to load from the key value store.
    EXPECT_CALL(*store, iterate).WillOnce(Invoke([&](KeyValueStore::ConstIterateCb fn) {
      fn("foo.com:80", "10.0.0.2:80|40|0");
      fn("bar.com:80", "1.1.1.1:1|20|1\n2.2.2.2:2|30|2");
      // No port.
      EXPECT_LOG_CONTAINS("warning", "Unable to parse cache line '1.1.1.1|20|1'",
                          fn("eep.com:80", "1.1.1.1|20|1"));
      // Won't be loaded because of prior error.
      fn("eep.com:80", "1.1.1.1|20|1:1");
    }));

    return ret;
  }));
  Registry::InjectFactory<KeyValueStoreFactory> injector(factory);
  auto* key_value_config = config_.mutable_key_value_config()->mutable_config();
  key_value_config->set_name("mock_key_value_store_factory");
  key_value_config->mutable_typed_config()->PackFrom(
      envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig());

  initialize();
  ASSERT(store != nullptr);
  EXPECT_EQ(2, TestUtility::findCounter(context_.store_, "dns_cache.foo.cache_load")->value());

  {
    MockLoadDnsCacheEntryCallbacks callbacks;
    auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, false, callbacks);
    EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
    EXPECT_EQ(result.handle_, nullptr);
    EXPECT_NE(absl::nullopt, result.host_info_);
    EXPECT_EQ(1, result.host_info_.value()->addressList().size());
  }

  {
    MockLoadDnsCacheEntryCallbacks callbacks;
    auto result = dns_cache_->loadDnsCacheEntry("bar.com", 80, false, callbacks);
    EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
    EXPECT_EQ(result.handle_, nullptr);
    ASSERT_NE(absl::nullopt, result.host_info_);
    EXPECT_EQ(2, result.host_info_.value()->addressList().size());
  }
}

// Make sure the cache manager can handle the context going out of scope.
TEST(DnsCacheManagerImplTest, TestLifetime) {
  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  std::unique_ptr<DnsCacheManagerImpl> cache_manager;

  {
    Server::GenericFactoryContextImpl scoped_context(context);
    cache_manager = std::make_unique<DnsCacheManagerImpl>(scoped_context);
  }
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config1;
  config1.set_name("foo");

  EXPECT_TRUE(cache_manager->getCache(config1).value() != nullptr);
}

TEST(NoramlizeHost, NormalizeHost) {
  EXPECT_EQ("localhost:80", DnsHostInfo::normalizeHostForDfp("localhost:80", 80));
  EXPECT_EQ("localhost:80", DnsHostInfo::normalizeHostForDfp("localhost:80", 443));
  EXPECT_EQ("localhost:443", DnsHostInfo::normalizeHostForDfp("localhost:443", 80));
  EXPECT_EQ("localhost:443", DnsHostInfo::normalizeHostForDfp("localhost:443", 443));
  EXPECT_EQ("localhost:123", DnsHostInfo::normalizeHostForDfp("localhost:123", 80));
  EXPECT_EQ("localhost:80", DnsHostInfo::normalizeHostForDfp("localhost", 80));
  EXPECT_EQ("localhost:443", DnsHostInfo::normalizeHostForDfp("localhost", 443));

  EXPECT_EQ("[fc00::1]:443", DnsHostInfo::normalizeHostForDfp("[fc00::1]:443", 80));
  EXPECT_EQ("[fc00::1]:80", DnsHostInfo::normalizeHostForDfp("[fc00::1]", 80));
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
