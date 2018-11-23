#include "envoy/http/conn_pool.h"

#include "common/http/wrapped/src_ip_transparent_mapper.h"
#include "common/network/utility.h"

#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

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
    auto retval = std::move(next_pool_to_assign_);
    next_pool_to_assign_ = std::make_unique<ConnectionPool::MockInstance>();
    return retval;
  }

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

} // namespace Http
} // namespace Envoy
