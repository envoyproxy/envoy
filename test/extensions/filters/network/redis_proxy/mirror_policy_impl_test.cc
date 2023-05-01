#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "source/extensions/filters/network/redis_proxy/mirror_policy_impl.h"

#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

using testing::Eq;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

TEST(MirrorPolicyImplTest, ShouldMirrorDefault) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
      RequestMirrorPolicy config;
  auto upstream = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_EQ(true, policy.shouldMirror("get"));
  EXPECT_EQ(true, policy.shouldMirror("set"));
  EXPECT_EQ(true, policy.shouldMirror("GET"));
  EXPECT_EQ(true, policy.shouldMirror("SET"));
}

TEST(MirrorPolicyImplTest, MissingUpstream) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
      RequestMirrorPolicy config;
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, nullptr, runtime);

  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(false, policy.shouldMirror("set"));
  EXPECT_EQ(false, policy.shouldMirror("GET"));
  EXPECT_EQ(false, policy.shouldMirror("SET"));
}

TEST(MirrorPolicyImplTest, ExcludeReadCommands) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
      RequestMirrorPolicy config;
  config.set_exclude_read_commands(true);
  auto upstream = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(true, policy.shouldMirror("set"));
  EXPECT_EQ(false, policy.shouldMirror("GET"));
  EXPECT_EQ(true, policy.shouldMirror("SET"));
}

TEST(MirrorPolicyImplTest, DefaultValueZero) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
      RequestMirrorPolicy config;
  auto* runtime_fraction = config.mutable_runtime_fraction();
  auto* percentage = runtime_fraction->mutable_default_value();
  percentage->set_numerator(0);
  percentage->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  auto upstream = std::make_shared<ConnPool::MockInstance>();
  NiceMock<Runtime::MockLoader> runtime;

  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(false, policy.shouldMirror("set"));
}

TEST(MirrorPolicyImplTest, DeterminedByRuntimeFraction) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
      RequestMirrorPolicy config;
  auto* runtime_fraction = config.mutable_runtime_fraction();
  runtime_fraction->set_runtime_key("runtime_key");
  auto* percentage = runtime_fraction->mutable_default_value();
  percentage->set_numerator(50);
  percentage->set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  auto upstream = std::make_shared<ConnPool::MockInstance>();

  NiceMock<Runtime::MockLoader> runtime;
  MirrorPolicyImpl policy(config, upstream, runtime);

  EXPECT_CALL(
      runtime.snapshot_,
      featureEnabled("runtime_key",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(50))))
      .Times(4)
      .WillRepeatedly(Return(true));
  EXPECT_EQ(true, policy.shouldMirror("get"));
  EXPECT_EQ(true, policy.shouldMirror("set"));
  EXPECT_EQ(true, policy.shouldMirror("GET"));
  EXPECT_EQ(true, policy.shouldMirror("SET"));

  EXPECT_CALL(
      runtime.snapshot_,
      featureEnabled("runtime_key",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(50))))
      .Times(4)
      .WillRepeatedly(Return(false));
  EXPECT_EQ(false, policy.shouldMirror("get"));
  EXPECT_EQ(false, policy.shouldMirror("set"));
  EXPECT_EQ(false, policy.shouldMirror("GET"));
  EXPECT_EQ(false, policy.shouldMirror("SET"));
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

