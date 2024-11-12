#include "source/common/http/http_server_properties_cache_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Http {

namespace {

static const absl::optional<std::chrono::seconds> kNoTtl = absl::nullopt;
class HttpServerPropertiesCacheImplTest : public testing::TestWithParam<Envoy::MockKeyValueStore*> {
public:
  HttpServerPropertiesCacheImplTest()
      : dispatcher_([]() {
          Envoy::Test::Global<Event::SingletonTimeSystemHelper> time_system;
          time_system.get().timeSystem(
              []() { return std::make_unique<Event::SimulatedTimeSystemHelper>(); });
          return NiceMock<Event::MockDispatcher>();
        }()),
        store_(GetParam()), expiration1_(dispatcher_.timeSource().monotonicTime() + Seconds(5)),
        expiration2_(dispatcher_.timeSource().monotonicTime() + Seconds(10)),
        protocol1_(alpn1_, hostname1_, port1_, expiration1_),
        protocol2_(alpn2_, hostname2_, port2_, expiration2_), protocols1_({protocol1_}),
        protocols2_({protocol2_}) {
    ON_CALL(dispatcher_, approximateMonotonicTime()).WillByDefault(Invoke([this]() {
      return dispatcher_.timeSource().monotonicTime();
    }));
  }

  void initialize() {
    protocols_ = std::make_unique<HttpServerPropertiesCacheImpl>(
        dispatcher_, std::move(suffixes_), std::unique_ptr<KeyValueStore>(store_), max_entries_);
  }

  size_t max_entries_ = 10;

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<std::string> suffixes_;
  MockKeyValueStore* store_;
  std::unique_ptr<HttpServerPropertiesCacheImpl> protocols_;

  const std::string hostname1_ = "hostname1";
  const std::string hostname2_ = "hostname2";
  const std::string hostname3_ = "hostname3";
  const uint32_t port1_ = 1;
  const uint32_t port2_ = 2;
  const std::string https_ = "https";
  const std::string http_ = "http";

  const std::string alpn1_ = "alpn1";
  const std::string alpn2_ = "alpn2";

  const MonotonicTime expiration1_;
  const MonotonicTime expiration2_;

  const HttpServerPropertiesCacheImpl::Origin origin1_ = {https_, hostname1_, port1_};
  const HttpServerPropertiesCacheImpl::Origin origin2_ = {https_, hostname2_, port2_};
  const HttpServerPropertiesCacheImpl::Origin origin3_ = {https_, hostname3_, port2_};

  HttpServerPropertiesCacheImpl::AlternateProtocol protocol1_;
  HttpServerPropertiesCacheImpl::AlternateProtocol protocol2_;

  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols1_;
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols2_;
};

#define EXPECT_CALL_WHEN_STORE_VALID(method)                                                       \
  if (store_) {                                                                                    \
    EXPECT_CALL(*store_, method);                                                                  \
  }

TEST_P(HttpServerPropertiesCacheImplTest, Init) {
  initialize();
  EXPECT_EQ(0, protocols_->size());
}

TEST_P(HttpServerPropertiesCacheImplTest, SetAlternativesThenSrtt) {
  initialize();
  EXPECT_EQ(0, protocols_->size());
  EXPECT_EQ(std::chrono::microseconds(0), protocols_->getSrtt(origin1_));
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|0|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols1_);
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|5|0", kNoTtl));
  protocols_->setSrtt(origin1_, std::chrono::microseconds(5));
  EXPECT_EQ(1, protocols_->size());
  EXPECT_EQ(std::chrono::microseconds(5), protocols_->getSrtt(origin1_));
}

TEST_P(HttpServerPropertiesCacheImplTest, SetSrttThenAlternatives) {
  initialize();
  EXPECT_EQ(0, protocols_->size());
  EXPECT_CALL_WHEN_STORE_VALID(addOrUpdate("https://hostname1:1", "clear|5|0", kNoTtl));
  protocols_->setSrtt(origin1_, std::chrono::microseconds(5));
  EXPECT_EQ(1, protocols_->size());
  EXPECT_EQ(std::chrono::microseconds(5), protocols_->getSrtt(origin1_));
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|5|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols1_);
  EXPECT_EQ(std::chrono::microseconds(5), protocols_->getSrtt(origin1_));
}

TEST_P(HttpServerPropertiesCacheImplTest, SetConcurrency) {
  initialize();
  EXPECT_EQ(0, protocols_->size());
  EXPECT_CALL_WHEN_STORE_VALID(addOrUpdate("https://hostname1:1", "clear|0|5", kNoTtl));
  protocols_->setConcurrentStreams(origin1_, 5);
  EXPECT_EQ(5, protocols_->getConcurrentStreams(origin1_));
}

TEST_P(HttpServerPropertiesCacheImplTest, FindAlternatives) {
  initialize();
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|0|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols1_);
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());
}

TEST_P(HttpServerPropertiesCacheImplTest, FindAlternativesAfterReplacement) {
  initialize();
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|0|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols1_);
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn2=\"hostname2:2\"; ma=10|0|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols2_);
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_, protocols.ref());
  EXPECT_NE(protocols1_, protocols.ref());
}

TEST_P(HttpServerPropertiesCacheImplTest, FindAlternativesForMultipleOrigins) {
  initialize();
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|0|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols1_);
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname2:2", "alpn2=\"hostname2:2\"; ma=10|0|0", kNoTtl));
  protocols_->setAlternatives(origin2_, protocols2_);
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());

  protocols = protocols_->findAlternatives(origin2_);
  EXPECT_EQ(protocols2_, protocols.ref());
}

TEST_P(HttpServerPropertiesCacheImplTest, FindAlternativesAfterExpiration) {
  initialize();
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|0|0", kNoTtl));
  protocols_->setAlternatives(origin1_, protocols1_);
  dispatcher_.globalTimeSystem().advanceTimeWait(Seconds(6));
  EXPECT_CALL_WHEN_STORE_VALID(remove("https://hostname1:1"));
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_FALSE(protocols.has_value());
  EXPECT_EQ(1u, protocols_->size());
}

TEST_P(HttpServerPropertiesCacheImplTest, FindAlternativesAfterPartialExpiration) {
  initialize();
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1",
                  "alpn1=\"hostname1:1\"; ma=5,alpn2=\"hostname2:2\"; ma=10|0|0", kNoTtl));
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> both = {protocol1_, protocol2_};
  protocols_->setAlternatives(origin1_, both);
  dispatcher_.globalTimeSystem().advanceTimeWait(Seconds(6));
  EXPECT_CALL_WHEN_STORE_VALID(
      addOrUpdate("https://hostname1:1", "alpn2=\"hostname2:2\"; ma=10|0|0", kNoTtl));
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_.size(), protocols->size());
  EXPECT_EQ(protocols2_, protocols.ref());
}

TEST_P(HttpServerPropertiesCacheImplTest, FindAlternativesAfterTruncation) {
  initialize();
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> expected_protocols;
  for (size_t i = 0; i < 10; ++i) {
    protocol1_.port_++;
    expected_protocols.push_back(protocol1_);
  }
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> full_protocols = expected_protocols;
  protocol1_.port_++;
  full_protocols.push_back(protocol1_);
  full_protocols.push_back(protocol1_);

  protocols_->setAlternatives(origin1_, full_protocols);
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(10, protocols->size());
  EXPECT_EQ(expected_protocols, protocols.ref());
}

TEST_P(HttpServerPropertiesCacheImplTest, ToAndFromOriginString) {
  initialize();
  std::string origin_str = "https://hostname1:1";
  absl::optional<HttpServerPropertiesCache::Origin> origin =
      HttpServerPropertiesCacheImpl::stringToOrigin(origin_str);
  ASSERT_TRUE(origin.has_value());
  EXPECT_EQ(1, origin.value().port_);
  EXPECT_EQ("https", origin.value().scheme_);
  EXPECT_EQ("hostname1", origin.value().hostname_);
  std::string output = HttpServerPropertiesCacheImpl::originToString(origin.value());
  EXPECT_EQ(origin_str, output);

  // Test with no scheme or port.
  std::string origin_str2 = "://:1";
  absl::optional<HttpServerPropertiesCache::Origin> origin2 =
      HttpServerPropertiesCacheImpl::stringToOrigin(origin_str2);
  ASSERT_TRUE(origin2.has_value());
  EXPECT_EQ(1, origin2.value().port_);
  EXPECT_EQ("", origin2.value().scheme_);
  EXPECT_EQ("", origin2.value().hostname_);
  std::string output2 = HttpServerPropertiesCacheImpl::originToString(origin2.value());
  EXPECT_EQ(origin_str2, output2);

  // No port.
  EXPECT_TRUE(!HttpServerPropertiesCacheImpl::stringToOrigin("https://").has_value());
  // Non-numeric port.
  EXPECT_TRUE(!HttpServerPropertiesCacheImpl::stringToOrigin("://asd:dsa").has_value());
  // Negative port.
  EXPECT_TRUE(!HttpServerPropertiesCacheImpl::stringToOrigin("https://:-1").has_value());
}
TEST_P(HttpServerPropertiesCacheImplTest, MaxEntries) {
  // This test requires store. Do not execute it when store is not present.
  if (store_ == nullptr)
    return;
  initialize();
  EXPECT_EQ(0, protocols_->size());
  const std::string hostname = "hostname";
  for (uint32_t i = 0; i <= max_entries_; ++i) {
    const HttpServerPropertiesCache::Origin origin = {https_, hostname, i};
    HttpServerPropertiesCache::AlternateProtocol protocol = {alpn1_, hostname, i, expiration1_};
    std::vector<HttpServerPropertiesCache::AlternateProtocol> protocols = {protocol};
    EXPECT_CALL(*store_, addOrUpdate(absl::StrCat("https://hostname:", i),
                                     absl::StrCat("alpn1=\"hostname:", i, "\"; ma=5|0|0"), kNoTtl));
    if (i == max_entries_) {
      EXPECT_CALL(*store_, remove("https://hostname:0"));
    }
    protocols_->setAlternatives(origin, protocols);
  }
}

TEST_P(HttpServerPropertiesCacheImplTest, ToAndFromString) {
  initialize();
  auto testAltSvc = [&](const std::string& original_alt_svc,
                        const std::string& expected_alt_svc) -> void {
    absl::optional<HttpServerPropertiesCacheImpl::OriginData> origin_data =
        HttpServerPropertiesCacheImpl::originDataFromString(original_alt_svc,
                                                            dispatcher_.timeSource(), true);
    ASSERT(origin_data.has_value());
    std::vector<HttpServerPropertiesCache::AlternateProtocol>& protocols =
        origin_data.value().protocols.value();
    ASSERT_GE(protocols.size(), 1);
    HttpServerPropertiesCache::AlternateProtocol& protocol = protocols[0];
    EXPECT_EQ("h3-29", protocol.alpn_);
    EXPECT_EQ("", protocol.hostname_);
    EXPECT_EQ(443, protocol.port_);
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        protocol.expiration_ - dispatcher_.timeSource().monotonicTime());
    EXPECT_EQ(86400, duration.count());
    if (protocols.size() == 2) {
      HttpServerPropertiesCache::AlternateProtocol& protocol2 = protocols[1];
      EXPECT_EQ("h3", protocol2.alpn_);
      EXPECT_EQ("", protocol2.hostname_);
      EXPECT_EQ(443, protocol2.port_);
      duration = std::chrono::duration_cast<std::chrono::seconds>(
          protocol2.expiration_ - dispatcher_.timeSource().monotonicTime());
      EXPECT_EQ(60, duration.count());
    }

    std::string alt_svc =
        HttpServerPropertiesCacheImpl::originDataToStringForCache(origin_data.value());
    EXPECT_EQ(expected_alt_svc, alt_svc);
  };

  testAltSvc("h3-29=\":443\"; ma=86400|0|0", "h3-29=\":443\"; ma=86400|0|0");
  testAltSvc("h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60|2|0",
             "h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60|2|0");

  // Test once more to make sure we handle time advancing correctly.
  // the absolute expiration time in testAltSvc is expected to be 86400 so add
  // 60s to the default max age.
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::seconds(60));
  testAltSvc("h3-29=\":443\"; ma=86460|2000|0", "h3-29=\":443\"; ma=86460|2000|0");
}

TEST_P(HttpServerPropertiesCacheImplTest, InvalidString) {
  initialize();
  // Too many numbers
  EXPECT_FALSE(
      HttpServerPropertiesCacheImpl::originDataFromString(
          "h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60|1|2|3", dispatcher_.timeSource(), true)
          .has_value());
  // Non-numeric rtt
  EXPECT_FALSE(
      HttpServerPropertiesCacheImpl::originDataFromString(
          "h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60|a|1", dispatcher_.timeSource(), true)
          .has_value());
  // Non-numeric concurrency
  EXPECT_FALSE(
      HttpServerPropertiesCacheImpl::originDataFromString(
          "h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60|1|a", dispatcher_.timeSource(), true)
          .has_value());

  // Standard entry with rtt and concurrency.
  EXPECT_TRUE(HttpServerPropertiesCacheImpl::originDataFromString(
                  "h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60|1|2", dispatcher_.timeSource(), true)
                  .has_value());
}

TEST_P(HttpServerPropertiesCacheImplTest, CacheLoad) {
  // This test requires store. Do not execute it when store is not present.
  if (store_ == nullptr)
    return;
  EXPECT_CALL(*store_, iterate(_)).WillOnce(Invoke([&](KeyValueStore::ConstIterateCb fn) {
    fn("foo", "bar");
    fn("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|2|3");
  }));

  // When the cache is created, there should be a warning log for the bad cache
  // entry.
  EXPECT_LOG_CONTAINS("warn", "Unable to parse cache entry with key: foo value: bar",
                      { initialize(); });

  EXPECT_CALL(*store_, addOrUpdate(_, _, _)).Times(0);
  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());
  EXPECT_EQ(2, protocols_->getSrtt(origin1_).count());
  EXPECT_EQ(3, protocols_->getConcurrentStreams(origin1_));
}

TEST_P(HttpServerPropertiesCacheImplTest, CacheLoadSrttOnly) {
  // This test requires store. Do not execute it when store is not present.
  if (store_ == nullptr)
    return;
  EXPECT_CALL(*store_, iterate(_)).WillOnce(Invoke([&](KeyValueStore::ConstIterateCb fn) {
    fn("https://hostname1:1", "clear|5|0");
  }));
  initialize();

  EXPECT_CALL(*store_, addOrUpdate(_, _, _)).Times(0);
  ASSERT_FALSE(protocols_->findAlternatives(origin1_).has_value());
  EXPECT_EQ(std::chrono::microseconds(5), protocols_->getSrtt(origin1_));
}

TEST_P(HttpServerPropertiesCacheImplTest, ShouldNotUpdateStoreOnCacheLoad) {
  if (store_ != nullptr) {
    EXPECT_CALL(*store_, addOrUpdate(_, _, _)).Times(0);
    EXPECT_CALL(*store_, iterate(_)).WillOnce(Invoke([&](KeyValueStore::ConstIterateCb fn) {
      fn("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5|0|0");
    }));
  }
  initialize();
}

TEST_P(HttpServerPropertiesCacheImplTest, GetOrCreateHttp3StatusTracker) {
  max_entries_ = 1u;
  initialize();
  EXPECT_EQ(0u, protocols_->size());

  protocols_->getOrCreateHttp3StatusTracker(origin1_).markHttp3Broken();
  EXPECT_EQ(1u, protocols_->size());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin1_).isHttp3Broken());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin1_).isHttp3Broken());

  // Fetch HTTP/3 status for another origin should overwrite the cache because
  // it's limited to one entry.
  EXPECT_FALSE(protocols_->getOrCreateHttp3StatusTracker(origin2_).isHttp3Broken());
  EXPECT_EQ(1u, protocols_->size());
  EXPECT_FALSE(protocols_->getOrCreateHttp3StatusTracker(origin1_).isHttp3Broken());
}

TEST_P(HttpServerPropertiesCacheImplTest, ClearBrokenness) {
  initialize();
  EXPECT_EQ(0u, protocols_->size());

  protocols_->getOrCreateHttp3StatusTracker(origin1_).markHttp3Broken();
  protocols_->getOrCreateHttp3StatusTracker(origin2_).markHttp3Confirmed();
  protocols_->getOrCreateHttp3StatusTracker(origin3_).markHttp3Broken();

  EXPECT_EQ(3u, protocols_->size());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin1_).isHttp3Broken());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin3_).isHttp3Broken());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin2_).isHttp3Confirmed());

  protocols_->resetBrokenness();

  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin1_).hasHttp3FailedRecently());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin3_).hasHttp3FailedRecently());
  EXPECT_TRUE(protocols_->getOrCreateHttp3StatusTracker(origin2_).isHttp3Confirmed());
}

TEST_P(HttpServerPropertiesCacheImplTest, CanonicalSuffix) {
  std::string suffix = ".example.com";
  std::string host1 = "first.example.com";
  std::string host2 = "www.second.example.com";
  const HttpServerPropertiesCacheImpl::Origin origin1 = {https_, host1, port1_};
  const HttpServerPropertiesCacheImpl::Origin origin2 = {https_, host2, port2_};

  suffixes_.push_back(suffix);
  initialize();
  protocols_->setAlternatives(origin1, protocols1_);

  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin2);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());
}

TEST_P(HttpServerPropertiesCacheImplTest, CanonicalSuffixNoMatch) {
  std::string suffix = ".example.com";
  std::string host1 = "www.example.com";
  std::string host2 = "www.other.com";
  const HttpServerPropertiesCacheImpl::Origin origin1 = {https_, host1, port1_};
  const HttpServerPropertiesCacheImpl::Origin origin2 = {https_, host2, port2_};

  suffixes_.push_back(suffix);
  initialize();
  protocols_->setAlternatives(origin1, protocols1_);

  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin2);
  ASSERT_FALSE(protocols.has_value());
}

TEST_P(HttpServerPropertiesCacheImplTest, ExplicitAlternativeTakesPriorityOverCanonicalSuffix) {
  std::string suffix = ".example.com";
  std::string host1 = "first.example.com";
  std::string host2 = "second.example.com";
  const HttpServerPropertiesCacheImpl::Origin origin1 = {https_, host1, port1_};
  const HttpServerPropertiesCacheImpl::Origin origin2 = {https_, host2, port2_};

  suffixes_.push_back(suffix);
  initialize();
  protocols_->setAlternatives(origin1, protocols1_);
  protocols_->setAlternatives(origin2, protocols2_);

  OptRef<const std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol>> protocols =
      protocols_->findAlternatives(origin2);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_, protocols.ref());
}

// Execute all tests when key value store is nullptr and when it is valid.
INSTANTIATE_TEST_SUITE_P(HttpServerPropertiesCacheImplTestSuite, HttpServerPropertiesCacheImplTest,
                         testing::Values(nullptr, new NiceMock<MockKeyValueStore>()));
} // namespace
} // namespace Http
} // namespace Envoy
