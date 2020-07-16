#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/omit_canary_hosts/config.h"
#include "extensions/retry/host/well_known_names.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {
namespace {

TEST(OmitCanaryHostsRetryPredicateTest, PredicateTest) {
  auto factory = Registry::FactoryRegistry<Upstream::RetryHostPredicateFactory>::getFactory(
      RetryHostPredicateValues::get().OmitCanaryHostsPredicate);

  ASSERT_NE(nullptr, factory);

  ProtobufWkt::Struct config;
  auto predicate = factory->createHostPredicate(config, 3);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();

  ON_CALL(*host1, canary()).WillByDefault(Return(false));
  ON_CALL(*host2, canary()).WillByDefault(Return(true));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host1));
  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host2));
  predicate->onHostAttempted(host1);
}

TEST(OmitCanaryHostsRetryPredicateTest, EmptyConfig) {
  auto factory = Registry::FactoryRegistry<Upstream::RetryHostPredicateFactory>::getFactory(
      RetryHostPredicateValues::get().OmitCanaryHostsPredicate);

  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr config = factory->createEmptyConfigProto();
  EXPECT_TRUE(dynamic_cast<envoy::config::retry::omit_canary_hosts::v2::OmitCanaryHostsPredicate*>(
      config.get()));
}

} // namespace
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
