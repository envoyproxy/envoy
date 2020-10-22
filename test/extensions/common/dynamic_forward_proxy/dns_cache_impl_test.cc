#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "common/config/utility.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

using testing::InSequence;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {
namespace {

class DnsCacheImplTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  void initialize() {
    config_.set_name("foo");
    config_.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);

    EXPECT_CALL(dispatcher_, createDnsResolver(_, _)).WillOnce(Return(resolver_));
    dns_cache_ =
        std::make_unique<DnsCacheImpl>(dispatcher_, tls_, random_, loader_, store_, config_);
    update_callbacks_handle_ = dns_cache_->addUpdateCallbacks(update_callbacks_);
  }

  ~DnsCacheImplTest() override {
    dns_cache_.reset();
    EXPECT_EQ(0, TestUtility::findGauge(store_, "dns_cache.foo.num_hosts")->value());
  }

  void checkStats(uint64_t query_attempt, uint64_t query_success, uint64_t query_failure,
                  uint64_t address_changed, uint64_t added, uint64_t removed, uint64_t num_hosts) {
    const auto counter_value = [this](const std::string& name) {
      return TestUtility::findCounter(store_, "dns_cache.foo." + name)->value();
    };

    EXPECT_EQ(query_attempt, counter_value("dns_query_attempt"));
    EXPECT_EQ(query_success, counter_value("dns_query_success"));
    EXPECT_EQ(query_failure, counter_value("dns_query_failure"));
    EXPECT_EQ(address_changed, counter_value("host_address_changed"));
    EXPECT_EQ(added, counter_value("host_added"));
    EXPECT_EQ(removed, counter_value("host_removed"));
    EXPECT_EQ(num_hosts, TestUtility::findGauge(store_, "dns_cache.foo.num_hosts")->value());
  }

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> loader_;
  Stats::IsolatedStoreImpl store_;
  std::unique_ptr<DnsCache> dns_cache_;
  MockUpdateCallbacks update_callbacks_;
  DnsCache::AddUpdateCallbacksHandlePtr update_callbacks_handle_;
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

// Basic successful resolution and then re-resolution.
TEST_F(DnsCacheImplTest, ResolveSuccess) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve timer.
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  checkStats(2 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Address does not change.
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve timer.
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();

  checkStats(3 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Address does change.
  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.2:80", "foo.com", false)));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.2"}));

  checkStats(3 /* attempt */, 3 /* success */, 0 /* failure */, 2 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);
}

// Ipv4 address.
TEST_F(DnsCacheImplTest, Ipv4Address) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("127.0.0.1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("127.0.0.1", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("127.0.0.1", DnsHostInfoEquals("127.0.0.1:80", "127.0.0.1", true)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"127.0.0.1"}));
}

// Ipv4 address with port.
TEST_F(DnsCacheImplTest, Ipv4AddressWithPort) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("127.0.0.1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("127.0.0.1:10000", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("127.0.0.1:10000",
                                   DnsHostInfoEquals("127.0.0.1:10000", "127.0.0.1", true)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"127.0.0.1"}));
}

// Ipv6 address.
TEST_F(DnsCacheImplTest, Ipv6Address) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("::1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("[::1]", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("[::1]", DnsHostInfoEquals("[::1]:80", "::1", true)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"::1"}));
}

// Ipv6 address with port.
TEST_F(DnsCacheImplTest, Ipv6AddressWithPort) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("::1", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("[::1]:10000", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("[::1]:10000", DnsHostInfoEquals("[::1]:10000", "::1", true)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"::1"}));
}

// TTL purge test.
TEST_F(DnsCacheImplTest, TTL) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}, std::chrono::seconds(0)));

  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve with ~60s passed. TTL should still be OK at default of 5 minutes.
  simTime().advanceTimeWait(std::chrono::milliseconds(60001));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();
  checkStats(2 /* attempt */, 1 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));
  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(300000));
  EXPECT_CALL(update_callbacks_, onDnsHostRemove("foo.com"));
  resolve_timer->invokeCallback();
  checkStats(2 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             1 /* added */, 1 /* removed */, 0 /* num hosts */);

  // Make sure we don't get a cache hit the next time the host is requested.
  resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  checkStats(3 /* attempt */, 2 /* success */, 0 /* failure */, 1 /* address changed */,
             2 /* added */, 1 /* removed */, 1 /* num hosts */);
}

// TTL purge test with different refresh/TTL parameters.
TEST_F(DnsCacheImplTest, TTLWithCustomParameters) {
  *config_.mutable_dns_refresh_rate() = Protobuf::util::TimeUtil::SecondsToDuration(30);
  *config_.mutable_host_ttl() = Protobuf::util::TimeUtil::SecondsToDuration(60);
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(30000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}, std::chrono::seconds(0)));

  // Re-resolve with ~30s passed. TTL should still be OK at 60s.
  simTime().advanceTimeWait(std::chrono::milliseconds(30001));
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  resolve_timer->invokeCallback();
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(30000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  // Re-resolve with ~30s passed. TTL should expire.
  simTime().advanceTimeWait(std::chrono::milliseconds(30001));
  EXPECT_CALL(update_callbacks_, onDnsHostRemove("foo.com"));
  resolve_timer->invokeCallback();
}

// Resolve that completes inline without any callback.
TEST_F(DnsCacheImplTest, InlineResolve) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  auto result = dns_cache_->loadDnsCacheEntry("localhost", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("localhost", _, _))
      .WillOnce(Invoke([](const std::string&, Network::DnsLookupFamily,
                          Network::DnsResolver::ResolveCb callback) {
        callback(Network::DnsResolver::ResolutionStatus::Success,
                 TestUtility::makeDnsResponse({"127.0.0.1"}));
        return nullptr;
      }));
  EXPECT_CALL(
      update_callbacks_,
      onDnsHostAddOrUpdate("localhost", DnsHostInfoEquals("127.0.0.1:80", "localhost", false)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  post_cb();
}

// Resolve failure that returns no addresses.
TEST_F(DnsCacheImplTest, ResolveFailure) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Failure, TestUtility::makeDnsResponse({}));
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
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
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  ON_CALL(random_, random()).WillByDefault(Return(8000));
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(1000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Failure, TestUtility::makeDnsResponse({}));
  checkStats(1 /* attempt */, 0 /* success */, 1 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
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
  Event::MockTimer* resolve_timer = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);
  checkStats(1 /* attempt */, 0 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  // A successful empty resolution DOES NOT update the host information.
  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  EXPECT_CALL(*resolve_timer, enableTimer(std::chrono::milliseconds(60000), _));
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));
  checkStats(1 /* attempt */, 1 /* success */, 0 /* failure */, 0 /* address changed */,
             1 /* added */, 0 /* removed */, 1 /* num hosts */);

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);

  // Re-resolve with ~5m passed. This is not realistic as we would have re-resolved many times
  // during this period but it's good enough for the test.
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
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
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  result.handle_.reset();
  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
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
  auto result1 = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks1);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result1.status_);
  EXPECT_NE(result1.handle_, nullptr);

  MockLoadDnsCacheEntryCallbacks callbacks2;
  auto result2 = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks2);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result2.status_);
  EXPECT_NE(result2.handle_, nullptr);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks2, onLoadDnsCacheComplete());
  EXPECT_CALL(callbacks1, onLoadDnsCacheComplete());
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
  auto result1 = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks1);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result1.status_);
  EXPECT_NE(result1.handle_, nullptr);
  EXPECT_EQ(dns_cache_->getHost("foo.com"), absl::nullopt);

  MockLoadDnsCacheEntryCallbacks callbacks2;
  Network::DnsResolver::ResolveCb resolve_cb2;
  EXPECT_CALL(*resolver_, resolve("bar.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb2), Return(&resolver_->active_query_)));
  auto result2 = dns_cache_->loadDnsCacheEntry("bar.com", 443, callbacks2);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result2.status_);
  EXPECT_NE(result2.handle_, nullptr);
  EXPECT_EQ(dns_cache_->getHost("bar.com"), absl::nullopt);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("bar.com", DnsHostInfoEquals("10.0.0.1:443", "bar.com", false)));
  EXPECT_CALL(callbacks2, onLoadDnsCacheComplete());
  resolve_cb2(Network::DnsResolver::ResolutionStatus::Success,
              TestUtility::makeDnsResponse({"10.0.0.1"}));

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.2:80", "foo.com", false)));
  EXPECT_CALL(callbacks1, onLoadDnsCacheComplete());
  resolve_cb1(Network::DnsResolver::ResolutionStatus::Success,
              TestUtility::makeDnsResponse({"10.0.0.2"}));

  auto hosts = dns_cache_->hosts();
  EXPECT_EQ(2, hosts.size());
  EXPECT_THAT(hosts["bar.com"], DnsHostInfoEquals("10.0.0.1:443", "bar.com", false));
  EXPECT_THAT(hosts["foo.com"], DnsHostInfoEquals("10.0.0.2:80", "foo.com", false));

  EXPECT_TRUE(dns_cache_->getHost("bar.com").has_value());
  EXPECT_THAT(dns_cache_->getHost("bar.com").value(),
              DnsHostInfoEquals("10.0.0.1:443", "bar.com", false));
  EXPECT_TRUE(dns_cache_->getHost("foo.com").has_value());
  EXPECT_THAT(dns_cache_->getHost("foo.com").value(),
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
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(update_callbacks_,
              onDnsHostAddOrUpdate("foo.com", DnsHostInfoEquals("10.0.0.1:80", "foo.com", false)));
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"10.0.0.1"}));

  result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::InCache, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
}

// Make sure we destroy active queries if the cache goes away.
TEST_F(DnsCacheImplTest, CancelActiveQueriesOnDestroy) {
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve("foo.com", _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(resolver_->active_query_, cancel());
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
  auto result = dns_cache_->loadDnsCacheEntry("foo.com:abc", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Loading, result.status_);
  EXPECT_NE(result.handle_, nullptr);

  EXPECT_CALL(update_callbacks_, onDnsHostAddOrUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onLoadDnsCacheComplete());
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));
}

// Max host overflow.
TEST_F(DnsCacheImplTest, MaxHostOverflow) {
  config_.mutable_max_hosts()->set_value(0);
  initialize();
  InSequence s;

  MockLoadDnsCacheEntryCallbacks callbacks;
  auto result = dns_cache_->loadDnsCacheEntry("foo.com", 80, callbacks);
  EXPECT_EQ(DnsCache::LoadDnsCacheEntryStatus::Overflow, result.status_);
  EXPECT_EQ(result.handle_, nullptr);
  EXPECT_EQ(1, TestUtility::findCounter(store_, "dns_cache.foo.host_overflow")->value());
}

TEST_F(DnsCacheImplTest, CircuitBreakersNotInvoked) {
  initialize();

  auto raii_ptr = dns_cache_->canCreateDnsRequest(absl::nullopt);
  EXPECT_NE(raii_ptr.get(), nullptr);
}

TEST_F(DnsCacheImplTest, DnsCacheCircuitBreakersOverflow) {
  config_.mutable_dns_cache_circuit_breaker()->mutable_max_pending_requests()->set_value(0);
  initialize();

  auto raii_ptr = dns_cache_->canCreateDnsRequest(absl::nullopt);
  EXPECT_EQ(raii_ptr.get(), nullptr);
  EXPECT_EQ(1, TestUtility::findCounter(store_, "dns_cache.foo.dns_rq_pending_overflow")->value());
}

TEST_F(DnsCacheImplTest, ClustersCircuitBreakersOverflow) {
  initialize();
  NiceMock<Upstream::MockBasicResourceLimit> pending_requests_;

  EXPECT_CALL(pending_requests_, canCreate()).WillOnce(Return(false));
  auto raii_ptr = dns_cache_->canCreateDnsRequest(pending_requests_);
  EXPECT_EQ(raii_ptr.get(), nullptr);
  EXPECT_EQ(0, TestUtility::findCounter(store_, "dns_cache.foo.dns_rq_pending_overflow")->value());
}

TEST(DnsCacheImplOptionsTest, UseTcpForDnsLookupsOptionSet) {
  NiceMock<Event::MockDispatcher> dispatcher;
  std::shared_ptr<Network::MockDnsResolver> resolver{std::make_shared<Network::MockDnsResolver>()};
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Random::MockRandomGenerator> random;
  NiceMock<Runtime::MockLoader> loader;
  Stats::IsolatedStoreImpl store;

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;
  config.set_use_tcp_for_dns_lookups(true);
  EXPECT_CALL(dispatcher, createDnsResolver(_, true)).WillOnce(Return(resolver));
  DnsCacheImpl dns_cache_(dispatcher, tls, random, loader, store, config);
}

TEST(DnsCacheImplOptionsTest, UseTcpForDnsLookupsOptionUnSet) {
  NiceMock<Event::MockDispatcher> dispatcher;
  std::shared_ptr<Network::MockDnsResolver> resolver{std::make_shared<Network::MockDnsResolver>()};
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Random::MockRandomGenerator> random;
  NiceMock<Runtime::MockLoader> loader;
  Stats::IsolatedStoreImpl store;

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;
  config.set_use_tcp_for_dns_lookups(false);
  EXPECT_CALL(dispatcher, createDnsResolver(_, false)).WillOnce(Return(resolver));
  DnsCacheImpl dns_cache_(dispatcher, tls, random, loader, store, config);
}

// DNS cache manager config tests.
TEST(DnsCacheManagerImplTest, LoadViaConfig) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Random::MockRandomGenerator> random;
  NiceMock<Runtime::MockLoader> loader;
  Stats::IsolatedStoreImpl store;
  DnsCacheManagerImpl cache_manager(dispatcher, tls, random, loader, store);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config1;
  config1.set_name("foo");

  auto cache1 = cache_manager.getCache(config1);
  EXPECT_NE(cache1, nullptr);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config2;
  config2.set_name("foo");
  EXPECT_EQ(cache1, cache_manager.getCache(config2));

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config3;
  config3.set_name("bar");
  auto cache2 = cache_manager.getCache(config3);
  EXPECT_NE(cache2, nullptr);
  EXPECT_NE(cache1, cache2);

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config4;
  config4.set_name("foo");
  config4.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V6_ONLY);
  EXPECT_THROW_WITH_MESSAGE(cache_manager.getCache(config4), EnvoyException,
                            "config specified DNS cache 'foo' with different settings");
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

} // namespace
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
