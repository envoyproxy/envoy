#include "envoy/http/conn_pool.h"

#include "common/http/wrapped/src_ip_transparent_mapper.h"
#include "common/network/utility.h"

#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gtest/gtest.h"

using testing::_;
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

  //! Called when one of the default mappers constructed above creates a new pool.
  //! This will replace next_pool_to_assign_ with a new mock. This allows the tester to control each
  //! individual mock pool by keeping track of the assigned pools where necessary.
  //! @returns @c next_pool_to_assign_.
  std::unique_ptr<ConnectionPool::Instance> buildPool() {
    // set up tracking of the pool's callback so we can invoke them,
    drained_callbacks_.push_back([]() { FAIL() << "No callback registed"; });
    EXPECT_CALL(*next_pool_to_assign_, addDrainedCallback(_))
        .WillOnce(SaveArg<0>(&drained_callbacks_.back()));

    auto retval = std::move(next_pool_to_assign_);
    next_pool_to_assign_ = std::make_unique<ConnectionPool::MockInstance>();
    return retval;
  }

  //! Invokes the last callback registered with the Nth (zero-based) created pool. If the pool
  //! never registered a callback, this will fail.
  void drainPool(size_t pool_index) { drained_callbacks_[pool_index](); }

  //! Sets the connection to use the provided remote address
  //! @param The address to use in the form of "ip:port".
  void setRemoteAddressToUse(const std::string& address) {
    // The connection mock returns a reference to remote_address_ by default as its implementation
    // of remoteAddress(). So, we simply change the value.
    connection_mock_.remote_address_ = Network::Utility::resolveUrl("tcp://" + address);
  }

  static constexpr size_t DefaultMaxNumPools = 10;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_mock_;
  NiceMock<Network::MockConnection> connection_mock_;
  std::unique_ptr<ConnectionPool::MockInstance> next_pool_to_assign_{
      new ConnectionPool::MockInstance()};
  uint32_t unique_address = 1;

  std::vector<ConnectionPool::Instance::DrainedCb> drained_callbacks_;
};

constexpr size_t SrcIpTransparentMapperTest::DefaultMaxNumPools;

//! A simple test to show that we can assign pools using the provided builder and that it
//! actually works.
TEST_F(SrcIpTransparentMapperTest, AssignPoolBasic) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, protocol()).WillOnce(Return(Http::Protocol::Http2));

  ConnectionPool::Instance* pool = mapper->assignPool(lb_context_mock_);
  ASSERT_NE(pool, nullptr);
  EXPECT_EQ(pool->protocol(), Http::Protocol::Http2);
}

//! A simple test to show that we can assign multiple pools using the provided builder and that it
//! actually works.
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

//! A simple test to show that if drainPools is called and we have no pools, nothing unsavoury
//! occurs.
TEST_F(SrcIpTransparentMapperTest, drainPoolsNoPools) {
  auto mapper = makeDefaultMapper();

  mapper->drainPools();
}

//! A simple test to show that if drainPools is called and we have single pool, it is properly
//! drained.
TEST_F(SrcIpTransparentMapperTest, drainPoolsSinglePool) {
  auto mapper = makeDefaultMapper();

  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  mapper->assignPool(lb_context_mock_);

  mapper->drainPools();
}

//! Multiple pools? Multiple drains.
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

//! If we haven't assigned anything, all pools are idle.
TEST_F(SrcIpTransparentMapperTest, noPoolsImpliesIdlePools) {
  auto mapper = makeDefaultMapper();

  EXPECT_TRUE(mapper->allPoolsIdle());
}

//! If we hit the limit on the number of pools, nullptr is returned.
TEST_F(SrcIpTransparentMapperTest, poolLimitHit) {
  auto mapper = makeMapperCustomSize(1);

  setRemoteAddressToUse("1.0.0.2:123");
  mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("2.0.0.2:123");
  EXPECT_EQ(mapper->assignPool(lb_context_mock_), nullptr);
}

//! Test that if we return different IPs from the LB Context, we return different pools
TEST_F(SrcIpTransparentMapperTest, differentIpsDifferentPools) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.1:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("1.0.0.2:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool1, pool2);
}

//! Test that if we use the same IP, we get the same pool
TEST_F(SrcIpTransparentMapperTest, sameIpsSamePools) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("10.0.0.1:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("10.0.0.1:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(nullptr, pool1);
  EXPECT_EQ(pool1, pool2);
}

//! Test that if hit the limit on the number of pools, but we don't need to allocate a new one, we
//! get the previously assigned pool back.
TEST_F(SrcIpTransparentMapperTest, sameIpsAtLimitGivesAssigned) {
  auto mapper = makeMapperCustomSize(1);

  setRemoteAddressToUse("10.0.0.1:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("10.0.0.1:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool1, nullptr);
  EXPECT_EQ(pool1, pool2);
}

//! Test that if we return different IPs from the LB Context, we return different pools
TEST_F(SrcIpTransparentMapperTest, differentIpsDifferentPoolsV6) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[1::1]:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[2::2]:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(pool1, pool2);
}

//! Test that if we use the same IP, we get the same pool
TEST_F(SrcIpTransparentMapperTest, sameIpsSamePoolsV6) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("[10::1]:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  setRemoteAddressToUse("[10::1]:123");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_NE(nullptr, pool1);
  EXPECT_EQ(pool1, pool2);
}

//! Test that we drain properly against both v4 and 6
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

//! Test that if a pool indicates it is idle, it will later be reused
TEST_F(SrcIpTransparentMapperTest, idlePoolIsReused) {
  auto mapper = makeDefaultMapper();

  setRemoteAddressToUse("1.0.0.2:123");
  auto pool1 = mapper->assignPool(lb_context_mock_);
  drainPool(0);

  setRemoteAddressToUse("20.4.0.5:155");
  auto pool2 = mapper->assignPool(lb_context_mock_);

  EXPECT_EQ(pool1, pool2);
}

//! Test that if a pool indicates it is idle, then it is assigned, it won't be reused for the same
//! ipv4 address
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

//! Test that if a pool indicates it is idle, then it is assigned, it won't be reused for the same
//! ipv6 address
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

//! Show that if multiple pools are idle, they are both reassigned. Also make sure they don't get
//! destroyed by accessing one of their members.
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

//! Show that we do not prevent assignment if # idle + # active is greater than the maximum
//! destroyed by accessing one of their members.
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

//! Show that we only drain active pools.
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

} // namespace Http
} // namespace Envoy
