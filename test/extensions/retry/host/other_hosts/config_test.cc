#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/other_hosts/config.h"
#include "extensions/retry/host/well_known_names.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

struct TestHostPredicateFactoryCallback : public Upstream::RetryHostPredicateFactoryCallbacks {

  void addHostPredicate(Upstream::RetryHostPredicateSharedPtr host_predicate) {
    host_predicate_ = host_predicate;
  }

  Upstream::RetryHostPredicateSharedPtr host_predicate_;
};

TEST(OtherHostsRetryPredicateConfigTest, PredicateTest) {
  auto factory = Registry::FactoryRegistry<Upstream::RetryHostPredicateFactory>::getFactory(
      RetryHostPredicateValues::get().PreviousHostsPredicate);

  ASSERT_NE(nullptr, factory);

  TestHostPredicateFactoryCallback callback;
  ProtobufWkt::Struct config;
  factory->createHostPredicate(callback, config, 3);

  auto predicate = callback.host_predicate_;

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

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
