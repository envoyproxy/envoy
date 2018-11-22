#include "envoy/http/conn_pool.h"

#include "common/http/wrapped/src_ip_transparent_mapper.h"

#include "test/mocks/http/conn_pool.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {

class SrcIpTransparentMapperTest : public testing::Test {
public:
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

  static constexpr size_t DefaultMaxNumPools = 10;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_mock_;
  std::unique_ptr<ConnectionPool::MockInstance> next_pool_to_assign_{
      new ConnectionPool::MockInstance()};
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
  ConnectionPool::Instance* pool1 = mapper->assignPool(lb_context_mock_);
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

//! A simple test to show that if drainPools is called and we have single pool ,it is properly
//! drained
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
  mapper->assignPool(lb_context_mock_);
  EXPECT_CALL(*next_pool_to_assign_, drainConnections());
  mapper->assignPool(lb_context_mock_);

  mapper->drainPools();
}

//! if we haven't assigned anything, all pools are idle
TEST_F(SrcIpTransparentMapperTest, noPoolsImpliesIdlePools) {
  auto mapper = makeDefaultMapper();

  EXPECT_TRUE(mapper->allPoolsIdle());
}

//! If we hit the limit on the number of pools, nullptr is returned
TEST_F(SrcIpTransparentMapperTest, poolLimitHit) {
  auto mapper = makeMapperCustomSize(1);

  mapper->assignPool(lb_context_mock_);
  ASSERT_EQ(mapper->assignPool(lb_context_mock_), nullptr);
}
} // namespace Http
} // namespace Envoy
