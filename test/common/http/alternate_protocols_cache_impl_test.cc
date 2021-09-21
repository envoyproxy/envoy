#include "source/common/http/alternate_protocols_cache_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {

namespace {
class AlternateProtocolsCacheImplTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  AlternateProtocolsCacheImplTest()
      : store_(new NiceMock<MockKeyValueStore>()),
        protocols_(simTime(), std::unique_ptr<KeyValueStore>(store_)) {}

  MockKeyValueStore* store_;
  AlternateProtocolsCacheImpl protocols_;
  const std::string hostname1_ = "hostname1";
  const std::string hostname2_ = "hostname2";
  const uint32_t port1_ = 1;
  const uint32_t port2_ = 2;
  const std::string https_ = "https";
  const std::string http_ = "http";

  const std::string alpn1_ = "alpn1";
  const std::string alpn2_ = "alpn2";

  const MonotonicTime expiration1_ = simTime().monotonicTime() + Seconds(5);
  const MonotonicTime expiration2_ = simTime().monotonicTime() + Seconds(10);

  const AlternateProtocolsCacheImpl::Origin origin1_ = {https_, hostname1_, port1_};
  const AlternateProtocolsCacheImpl::Origin origin2_ = {https_, hostname2_, port2_};

  AlternateProtocolsCacheImpl::AlternateProtocol protocol1_ = {alpn1_, hostname1_, port1_,
                                                               expiration1_};
  AlternateProtocolsCacheImpl::AlternateProtocol protocol2_ = {alpn2_, hostname2_, port2_,
                                                               expiration2_};

  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> protocols1_ = {protocol1_};
  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> protocols2_ = {protocol2_};
};

TEST_F(AlternateProtocolsCacheImplTest, Init) { EXPECT_EQ(0, protocols_.size()); }

TEST_F(AlternateProtocolsCacheImplTest, SetAlternatives) {
  EXPECT_EQ(0, protocols_.size());
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5"));
  protocols_.setAlternatives(origin1_, protocols1_);
  EXPECT_EQ(1, protocols_.size());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternatives) {
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5"));
  protocols_.setAlternatives(origin1_, protocols1_);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterReplacement) {
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5"));
  protocols_.setAlternatives(origin1_, protocols1_);
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn2=\"hostname2:2\"; ma=10"));
  protocols_.setAlternatives(origin1_, protocols2_);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_, protocols.ref());
  EXPECT_NE(protocols1_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesForMultipleOrigins) {
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5"));
  protocols_.setAlternatives(origin1_, protocols1_);
  EXPECT_CALL(*store_, addOrUpdate("https://hostname2:2", "alpn2=\"hostname2:2\"; ma=10"));
  protocols_.setAlternatives(origin2_, protocols2_);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());

  protocols = protocols_.findAlternatives(origin2_);
  EXPECT_EQ(protocols2_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterExpiration) {
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn1=\"hostname1:1\"; ma=5"));
  protocols_.setAlternatives(origin1_, protocols1_);
  simTime().setMonotonicTime(expiration1_ + Seconds(1));
  EXPECT_CALL(*store_, remove("https://hostname1:1"));
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_FALSE(protocols.has_value());
  EXPECT_EQ(0, protocols_.size());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterPartialExpiration) {
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1",
                                   "alpn1=\"hostname1:1\"; ma=5,alpn2=\"hostname2:2\"; ma=10"));
  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> both = {protocol1_, protocol2_};
  protocols_.setAlternatives(origin1_, both);
  simTime().setMonotonicTime(expiration1_ + Seconds(1));
  EXPECT_CALL(*store_, addOrUpdate("https://hostname1:1", "alpn2=\"hostname2:2\"; ma=10"));
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_.size(), protocols->size());
  EXPECT_EQ(protocols2_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterTruncation) {
  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> expected_protocols;
  for (size_t i = 0; i < 10; ++i) {
    protocol1_.port_++;
    expected_protocols.push_back(protocol1_);
  }
  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> full_protocols = expected_protocols;
  protocol1_.port_++;
  full_protocols.push_back(protocol1_);
  full_protocols.push_back(protocol1_);

  protocols_.setAlternatives(origin1_, full_protocols);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(10, protocols->size());
  EXPECT_EQ(expected_protocols, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, ToAndFromString) {
  auto testAltSvc = [&](const std::string& original_alt_svc,
                        const std::string& expected_alt_svc) -> void {
    absl::optional<std::vector<AlternateProtocolsCache::AlternateProtocol>> protocols =
        AlternateProtocolsCacheImpl::protocolsFromString(original_alt_svc, simTime(), true);
    ASSERT(protocols.has_value());
    ASSERT_GE(protocols.value().size(), 1);

    AlternateProtocolsCache::AlternateProtocol& protocol = protocols.value()[0];
    EXPECT_EQ("h3-29", protocol.alpn_);
    EXPECT_EQ("", protocol.hostname_);
    EXPECT_EQ(443, protocol.port_);
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(protocol.expiration_ -
                                                                     simTime().monotonicTime());
    EXPECT_EQ(86400, duration.count());

    if (protocols.value().size() == 2) {
      AlternateProtocolsCache::AlternateProtocol& protocol2 = protocols.value()[1];
      EXPECT_EQ("h3", protocol2.alpn_);
      EXPECT_EQ("", protocol2.hostname_);
      EXPECT_EQ(443, protocol2.port_);
      duration = std::chrono::duration_cast<std::chrono::seconds>(protocol2.expiration_ -
                                                                  simTime().monotonicTime());
      EXPECT_EQ(60, duration.count());
    }

    std::string alt_svc =
        AlternateProtocolsCacheImpl::protocolsToStringForCache(protocols.value(), simTime());
    EXPECT_EQ(expected_alt_svc, alt_svc);
  };

  testAltSvc("h3-29=\":443\"; ma=86400", "h3-29=\":443\"; ma=86400");
  testAltSvc("h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60",
             "h3-29=\":443\"; ma=86400,h3=\":443\"; ma=60");

  // Test once more to make sure we handle time advancing correctly.
  // the absolute expiration time in testAltSvc is expected to be 86400 so add
  // 60s to the default max age.
  simTime().setMonotonicTime(simTime().monotonicTime() + std::chrono::seconds(60));
  testAltSvc("h3-29=\":443\"; ma=86460", "h3-29=\":443\"; ma=86460");
}

} // namespace
} // namespace Http
} // namespace Envoy
