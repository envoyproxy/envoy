#include <functional>

#include "envoy/http/conn_pool.h"

#include "common/http/wrapped/src_ip_transparent_mapper.h"
#include "common/network/utility.h"

#include "test/mocks/common.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Field;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Http {

class SrcIpTransparentMapperTest : public testing::Test {
public:
  SrcIpTransparentMapperTest() {
    ON_CALL(lb_context_mock_, downstreamConnection()).WillByDefault(Return(&connection_mock_));
  }

  std::unique_ptr<SrcIpTransparentMapper> makeMapperCustomSize(size_t maxSize) {
    auto builder = [this]() { return buildPool(); };
    return std::make_unique<SrcIpTransparentMapper>(builder, maxSize);
  }

  std::unique_ptr<SrcIpTransparentMapper> makeDefaultMapper() {
    auto builder = [this]() { return buildPool(); };
    return std::make_unique<SrcIpTransparentMapper>(builder, DefaultMaxNumPools);
  }

  std::unique_ptr<SrcIpTransparentMapper> makeMapperWithImmediateCallback() {
    auto builder = [this]() { return buildPoolImmediateCallback(); };
    return std::make_unique<SrcIpTransparentMapper>(builder, DefaultMaxNumPools);
  }

  /**
   * Called when one of the default mappers constructed above creates a new pool.
   * This will replace next_pool_to_assign_ with a new mock. This allows the tester to control each
   * individual mock pool by keeping track of the assigned pools where necessary.
   * @returns @c next_pool_to_assign_.
   */
  std::unique_ptr<ConnectionPool::Instance> buildPool() {
    // set up tracking of the pool's callback so we can invoke them.
    drained_callbacks_.push_back([]() { FAIL() << "No callback registered"; });
    EXPECT_CALL(*next_pool_to_assign_, addDrainedCallback(_))
        .WillOnce(SaveArg<0>(&drained_callbacks_.back()));

    return pushPool();
  }

  /**
   * Called when we want a pool which calls back immediately on callbacks being added.
   * This will replace next_pool_to_assign_ with a new mock. This allows the tester to control each
   * individual mock pool by keeping track of the assigned pools where necessary.
   * @returns @c next_pool_to_assign_.
   */
  std::unique_ptr<ConnectionPool::Instance> buildPoolImmediateCallback() {
    // set up tracking of the pool's callback so we can invoke them.
    drained_callbacks_.push_back([]() { FAIL() << "No callback registered"; });
    EXPECT_CALL(*next_pool_to_assign_, addDrainedCallback(_))
        .WillOnce(DoAll(SaveArg<0>(&drained_callbacks_.back()),
                        Invoke([](ConnectionPool::Instance::DrainedCb cb) { cb(); })));
    return pushPool();
  }

  std::unique_ptr<ConnectionPool::Instance> pushPool() {
    auto retval = std::move(next_pool_to_assign_);
    next_pool_to_assign_ = std::make_unique<NiceMock<ConnectionPool::MockInstance>>();
    return retval;
  }

  /**
   * Invokes the last callback registered with the Nth (zero-based) created pool. If the pool
   * never registered a callback, this will fail.
   */
  void drainPool(size_t pool_index) { drained_callbacks_[pool_index](); }

  /**
   * Sets the connection to use the provided remote address
   * @param The address to use in the form of "ip:port".
   */
  void setRemoteAddressToUse(const std::string& address) {
    // The connection mock returns a reference to remote_address_ by default as its implementation
    // of remoteAddress(). So, we simply change the value.
    connection_mock_.remote_address_ = Network::Utility::resolveUrl("tcp://" + address);
    source_info_.source_address_ = connection_mock_.remote_address_;
  }

  static constexpr size_t DefaultMaxNumPools = 10;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_mock_;
  NiceMock<Network::MockConnection> connection_mock_;
  std::unique_ptr<NiceMock<ConnectionPool::MockInstance>> next_pool_to_assign_{
      new NiceMock<ConnectionPool::MockInstance>()};
  uint32_t unique_address = 1;
  std::vector<ConnectionPool::Instance::DrainedCb> drained_callbacks_;
  ConnectionPool::UpstreamSourceInformation source_info_;
};

constexpr size_t SrcIpTransparentMapperTest::DefaultMaxNumPools;

// A simple test to show that we can assign pools using the provided builder and that it
// actually works.
TEST_F(SrcIpTransparentMapperTest, AssignPoolBasic) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, protocol()).WillOnce(Return(Http::Protocol::Http2));

  ConnectionPool::Instance* pool = mapper->assignPool(lb_context_mock_);
  ASSERT_NE(pool, nullptr);
  EXPECT_EQ(pool->protocol(), Http::Protocol::Http2);
}

// A simple test to show that we can assign multiple pools using the provided builder and that it
// actually works.
TEST_F(SrcIpTransparentMapperTest, AssignPoolMulipleBasic) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, protocol()).WillOnce(Return(Http::Protocol::Http2));
  setRemoteAddressToUse("1.0.0.2:123");
  ConnectionPool::Instance* pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("1.0.0.3:123");
  EXPECT_CALL(*next_pool_to_assign_, protocol()).WillOnce(Return(Http::Protocol::Http11));
  ConnectionPool::Instance* pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(pool1->protocol(), Http::Protocol::Http2);
  EXPECT_EQ(pool2->protocol(), Http::Protocol::Http11);
}

// A simple test to show that if drainPools is called and we have no pools, nothing unsavoury
// occurs.
TEST_F(SrcIpTransparentMapperTest, drainPoolsNoPools) {
  auto mapper = makeDefaultMapper();

  mapper->drainPools();
}

// A simple test to show that if drainPools is called and we have single pool, it is properly
// drained.
TEST_F(SrcIpTransparentMapperTest, drainPoolsSinglePool) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  mapper->assignPool(lb_context_mock_);

  mapper->drainPools();
}

// Multiple pools? Multiple drains.
TEST_F(SrcIpTransparentMapperTest, drainPoolsMultiplePools) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  setRemoteAddressToUse("1.0.0.3:123");
  mapper->assignPool(lb_context_mock_);

  mapper->drainPools();
}

// If we haven't assigned anything, all pools are idle.
TEST_F(SrcIpTransparentMapperTest, noPoolsImpliesIdlePools) {
  auto mapper = makeDefaultMapper();

  EXPECT_TRUE(mapper->allPoolsIdle());
}

// If we hit the limit on the number of pools, nullptr is returned.
TEST_F(SrcIpTransparentMapperTest, poolLimitHit) {
  auto mapper = makeMapperCustomSize(1);

  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("2.0.0.2:123");
  EXPECT_EQ(mapper->assignPool(lb_context_mock_), nullptr);
}

// Test that if we return different IPs from the LB Context, we return different pools
TEST_F(SrcIpTransparentMapperTest, differentIpsDifferentPools) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.1:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("1.0.0.2:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool1, pool2);
}

// Test that if we use the same IP, we get the same pool
TEST_F(SrcIpTransparentMapperTest, sameIpsSamePools) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("10.0.0.1:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("10.0.0.1:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(nullptr, pool1);
  EXPECT_EQ(pool1, pool2);
}

// Test that if hit the limit on the number of pools, but we don't need to allocate a new one, we
// get the previously assigned pool back.
TEST_F(SrcIpTransparentMapperTest, sameIpsAtLimitGivesAssigned) {
  auto mapper = makeMapperCustomSize(1);

  setRemoteAddressToUse("10.0.0.1:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("10.0.0.1:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool1, nullptr);
  EXPECT_EQ(pool1, pool2);
}

// Test that if we return different IPs from the LB Context, we return different pools
TEST_F(SrcIpTransparentMapperTest, differentIpsDifferentPoolsV6) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[1::1]:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[2::2]:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool1, pool2);
}

// Test that if we use the same IP, we get the same pool
TEST_F(SrcIpTransparentMapperTest, sameIpsSamePoolsV6) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[10::1]:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[10::1]:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(nullptr, pool1);
  EXPECT_EQ(pool1, pool2);
}

// Test that we drain properly against both v4 and 6
TEST_F(SrcIpTransparentMapperTest, drainPoolsMixedV4AndV6) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  setRemoteAddressToUse("[12::34]:8541");
  mapper->assignPool(lb_context_mock_);

  mapper->drainPools();
}

// Test that if a pool indicates it is idle, it will later be reused
TEST_F(SrcIpTransparentMapperTest, idlePoolIsReused) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("20.4.0.5:155");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(pool1, pool2);
}

// Test that if a pool indicates it is idle, then it is assigned, it won't be reused for the same
// ipv4 address
TEST_F(SrcIpTransparentMapperTest, reassignedIdlePoolIsNotReused) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("20.4.0.5:155");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  setRemoteAddressToUse("1.0.0.2:123");
  auto pool3 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool3, nullptr);
  EXPECT_NE(pool3, pool2);
}

// Test that if a pool indicates it is idle, then it is assigned, it won't be reused for the same
// ipv6 address
TEST_F(SrcIpTransparentMapperTest, reassignedIdlePoolIsNotReusedV6) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[1234::6789]:123");
  mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("20.4.0.5:155");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  setRemoteAddressToUse("[1234::6789]:123");
  auto pool3 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool3, nullptr);
  EXPECT_NE(pool3, pool2);
}

// Show that if multiple pools are idle, they are both reassigned. Also make sure they don't get
// destroyed by accessing one of their members.
TEST_F(SrcIpTransparentMapperTest, testMultipleIdle) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  EXPECT_CALL(*next_pool_to_assign_, protocol()).WillOnce(Return(Http::Protocol::Http2));
  auto pool1 = mapper->assignPool(lb_context_mock_);
  EXPECT_CALL(*next_pool_to_assign_, protocol()).WillOnce(Return(Http::Protocol::Http11));
  setRemoteAddressToUse("[12::34]:8541");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  // drain in the reverse order so that the stack will assign them in the same order. This assumes
  // that behaviour, whitebox-style. Simple to use a set if order becomes arbitrary in the future.
  drainPool(1);
  drainPool(0);

  setRemoteAddressToUse("5.0.0.2:123");
  auto second_pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("7.0.0.2:123");
  auto second_pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(pool1, second_pool1);
  EXPECT_EQ(pool2, second_pool2);
  EXPECT_EQ(second_pool1->protocol(), Http::Protocol::Http2);
  EXPECT_EQ(second_pool2->protocol(), Http::Protocol::Http11);
}

// Show that we do not prevent assignment if # idle + # active is greater than the maximum
// destroyed by accessing one of their members.
TEST_F(SrcIpTransparentMapperTest, testIdleAndActiveAboveMax) {
  auto mapper = makeMapperCustomSize(2);

  setRemoteAddressToUse("1.0.0.2:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[12::34]:8541");
  mapper->assignPool(lb_context_mock_);

  // drain a pool so we can assign one.
  drainPool(0);

  setRemoteAddressToUse("5.0.0.2:123");
  auto second_pool1 = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(pool1, second_pool1);
}

// Show that we only drain active pools.
TEST_F(SrcIpTransparentMapperTest, idlePoolsNotDrained) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[12::34]:8541");
  EXPECT_CALL(*next_pool_to_assign_, drainConnections()).Times(0);
  mapper->assignPool(lb_context_mock_);

  // make the pool we don't expect to be drained idle.
  drainPool(1);

  mapper->drainPools();
}

// Test that if a pool goes idle, a callback is invoked.
TEST_F(SrcIpTransparentMapperTest, idleCallbackInvoked) {
  auto mapper = makeDefaultMapper();
  ReadyWatcher callback;

  mapper->addIdleCallback(std::bind(&ReadyWatcher::ready, &callback));
  mapper->assignPool(lb_context_mock_);

  EXPECT_CALL(callback, ready());
  drainPool(0);
}

TEST_F(SrcIpTransparentMapperTest, idlePoolsAndActiveMeansNotAllPoolsIdle) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[12::34]:8541");
  mapper->assignPool(lb_context_mock_);

  drainPool(0);
  EXPECT_FALSE(mapper->allPoolsIdle());
}

TEST_F(SrcIpTransparentMapperTest, idlePoolsAndNoActiveMeansPoolsIdle) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[12::34]:8541");
  mapper->assignPool(lb_context_mock_);

  drainPool(0);
  drainPool(1);
  EXPECT_TRUE(mapper->allPoolsIdle());
}

TEST_F(SrcIpTransparentMapperTest, noDownstreamAddressThrows) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(lb_context_mock_, downstreamConnection()).WillOnce(Return(nullptr));
  EXPECT_THROW(mapper->assignPool(lb_context_mock_), BadDownstreamConnectionException);
}

TEST_F(SrcIpTransparentMapperTest, noIpAddressThrows) {
  auto mapper = makeDefaultMapper();

  connection_mock_.remote_address_ = Network::Utility::resolveUrl("unix://foo/bar");
  EXPECT_THROW(mapper->assignPool(lb_context_mock_), BadDownstreamConnectionException);
}

// Show that we don't accidentally "idle" pools whose earlier assignments are mapped to new pools.
TEST_F(SrcIpTransparentMapperTest, noLingeringStateAfterIdle) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.2.3.4:567");
  mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("[1234::6789]:123");
  mapper->assignPool(lb_context_mock_);

  setRemoteAddressToUse("1.2.3.4:567");
  auto new_pool_old_addr = mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("1.2.3.4:567");
  auto should_be_same = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(new_pool_old_addr, should_be_same);
}

// Show that we don't accidentally "idle" pools whose earlier assignments are mapped to new pools.
TEST_F(SrcIpTransparentMapperTest, noLingeringStateAfterIdlev6) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[1234::6789]:123");
  mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("1.2.3.4:567");
  mapper->assignPool(lb_context_mock_);

  setRemoteAddressToUse("[1234::6789]:123");
  auto new_pool_old_addr = mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("[1234::6789]:123");
  auto should_be_same = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(new_pool_old_addr, should_be_same);
}

// Show that we tell the pool to use the remote address when establishing its connections.
TEST_F(SrcIpTransparentMapperTest, remoteAddressSentToPool) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:0");
  EXPECT_CALL(*next_pool_to_assign_,
              setUpstreamSourceInformation(
                  Field(&ConnectionPool::UpstreamSourceInformation::source_address_,
                        PointeesEq(source_info_.source_address_))));
  mapper->assignPool(lb_context_mock_);
}

// Show that even if the pool was previously assigned, it'll be updated with the latest address.
TEST_F(SrcIpTransparentMapperTest, remoteAddressUpdated) {
  auto mapper = makeDefaultMapper();

  const auto pool = next_pool_to_assign_.get();
  setRemoteAddressToUse("1.0.0.2:0");
  mapper->assignPool(lb_context_mock_);
  drainPool(0);
  setRemoteAddressToUse("2.9.71.5:0");
  EXPECT_CALL(*pool, setUpstreamSourceInformation(
                         Field(&ConnectionPool::UpstreamSourceInformation::source_address_,
                               PointeesEq(source_info_.source_address_))));
  mapper->assignPool(lb_context_mock_);
}

// Show that the port is masked off, since we're only setting the IP.
TEST_F(SrcIpTransparentMapperTest, portIgnored) {
  auto mapper = makeDefaultMapper();

  const auto pool = next_pool_to_assign_.get();
  setRemoteAddressToUse("9.0.0.1:123");
  EXPECT_CALL(*pool, setUpstreamSourceInformation(
                         Field(&ConnectionPool::UpstreamSourceInformation::source_address_,
                               PointeesEq(Network::Utility::parseInternetAddress("9.0.0.1")))));
  mapper->assignPool(lb_context_mock_);
}

// Make sure IPv6 is set properly
TEST_F(SrcIpTransparentMapperTest, remoteAddressIpv6) {
  auto mapper = makeDefaultMapper();

  const auto pool = next_pool_to_assign_.get();
  setRemoteAddressToUse("[1::2]:123");
  EXPECT_CALL(*pool, setUpstreamSourceInformation(
                         Field(&ConnectionPool::UpstreamSourceInformation::source_address_,
                               PointeesEq(Network::Utility::parseInternetAddress("1::2")))));
  mapper->assignPool(lb_context_mock_);
}

// Some connection pool instances may call back immediately when the drain cb is registered. Make
// sure we handle that by instructing the pool mock to callback when the cb is registered. To show
// that we handle it, we show that we can assign the same pool twice, and that it still drains
// correctly.
TEST_F(SrcIpTransparentMapperTest, immediateDrainHandled) {
  auto mapper = makeMapperWithImmediateCallback();

  setRemoteAddressToUse("[10::1]:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[10::1]:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);
  drainPool(0);
  setRemoteAddressToUse("12.35.16.41:546");
  auto pool3 = mapper->assignPool(lb_context_mock_);

  // Show that we always get back the same connection pool, including after draining (meaning we
  // freed the pool up)
  EXPECT_NE(nullptr, pool1);
  EXPECT_EQ(pool1, pool2);
  EXPECT_EQ(pool2, pool3);
}

// Show that we ignore the port when mapping the address to a pool.
TEST_F(SrcIpTransparentMapperTest, portIgnoredInMapping) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[123::4]:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[123::4]:777");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(nullptr, pool1);
  EXPECT_EQ(pool1, pool2);
}

} // namespace Http
} // namespace Envoy
