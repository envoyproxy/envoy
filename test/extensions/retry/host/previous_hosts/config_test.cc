#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/retry/host/previous_hosts/config.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {
namespace {

TEST(PreviousHostsRetryPredicateConfigTest, PredicateTest) {
  auto factory = Registry::FactoryRegistry<Upstream::RetryHostPredicateFactory>::getFactory(
      "envoy.retry_host_predicates.previous_hosts");

  ASSERT_NE(nullptr, factory);

  ProtobufWkt::Struct config;
  auto predicate = factory->createHostPredicate(config, 3);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto host1_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 123);
  ON_CALL(*host1, address()).WillByDefault(Return(host1_address));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto host2_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 456);
  ON_CALL(*host2, address()).WillByDefault(Return(host2_address));

  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host1));
  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host2));

  predicate->onHostAttempted(host1);

  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host1));
  ASSERT_FALSE(predicate->shouldSelectAnotherHost(*host2));

  predicate->onHostAttempted(host2);

  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host1));
  ASSERT_TRUE(predicate->shouldSelectAnotherHost(*host2));
}

TEST(PreviousHostsRetryPredicateConfigTest, EmptyConfig) {
  auto factory = Registry::FactoryRegistry<Upstream::RetryHostPredicateFactory>::getFactory(
      "envoy.retry_host_predicates.previous_hosts");

  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr config = factory->createEmptyConfigProto();
  EXPECT_TRUE(
      dynamic_cast<envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate*>(
          config.get()));
}

} // namespace
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
