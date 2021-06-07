#include "source/common/http/alternate_protocols_cache_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

namespace {
class AlternateProtocolsCacheImplTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  AlternateProtocolsCacheImplTest() : protocols_(simTime()) {}

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

  const AlternateProtocolsCacheImpl::AlternateProtocol protocol1_ = {alpn1_, hostname1_, port1_,
                                                                     expiration1_};
  const AlternateProtocolsCacheImpl::AlternateProtocol protocol2_ = {alpn2_, hostname2_, port2_,
                                                                     expiration2_};

  const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> protocols1_ = {protocol1_};
  const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> protocols2_ = {protocol2_};
};

TEST_F(AlternateProtocolsCacheImplTest, Init) { EXPECT_EQ(0, protocols_.size()); }

TEST_F(AlternateProtocolsCacheImplTest, SetAlternatives) {
  EXPECT_EQ(0, protocols_.size());
  protocols_.setAlternatives(origin1_, protocols1_);
  EXPECT_EQ(1, protocols_.size());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternatives) {
  protocols_.setAlternatives(origin1_, protocols1_);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterReplacement) {
  protocols_.setAlternatives(origin1_, protocols1_);
  protocols_.setAlternatives(origin1_, protocols2_);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_, protocols.ref());
  EXPECT_NE(protocols1_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesForMultipleOrigins) {
  protocols_.setAlternatives(origin1_, protocols1_);
  protocols_.setAlternatives(origin2_, protocols2_);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols1_, protocols.ref());

  protocols = protocols_.findAlternatives(origin2_);
  EXPECT_EQ(protocols2_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterExpiration) {
  protocols_.setAlternatives(origin1_, protocols1_);
  simTime().setMonotonicTime(expiration1_ + Seconds(1));
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_FALSE(protocols.has_value());
  EXPECT_EQ(0, protocols_.size());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterPartialExpiration) {
  protocols_.setAlternatives(origin1_, {protocol1_, protocol2_});
  simTime().setMonotonicTime(expiration1_ + Seconds(1));
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(protocols2_.size(), protocols->size());
  EXPECT_EQ(protocols2_, protocols.ref());
}

TEST_F(AlternateProtocolsCacheImplTest, FindAlternativesAfterTruncation) {
  AlternateProtocolsCacheImpl::AlternateProtocol protocol = protocol1_;

  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> expected_protocols;
  for (size_t i = 0; i < 10; ++i) {
    protocol.port_++;
    expected_protocols.push_back(protocol);
  }
  std::vector<AlternateProtocolsCacheImpl::AlternateProtocol> full_protocols = expected_protocols;
  protocol.port_++;
  full_protocols.push_back(protocol);
  full_protocols.push_back(protocol);

  protocols_.setAlternatives(origin1_, full_protocols);
  OptRef<const std::vector<AlternateProtocolsCacheImpl::AlternateProtocol>> protocols =
      protocols_.findAlternatives(origin1_);
  ASSERT_TRUE(protocols.has_value());
  EXPECT_EQ(10, protocols->size());
  EXPECT_EQ(expected_protocols, protocols.ref());
}

} // namespace
} // namespace Http
} // namespace Envoy
