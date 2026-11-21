#include "source/common/config/ecds_subscription_registry.h"
#include "source/common/singleton/manager_impl.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(EcdsSubscriptionRegistryTest, RejectsMalformedXdsTpNamesWithoutReservingThem) {
  Singleton::ManagerImpl manager;
  auto registry = EcdsSubscriptionRegistry::get(manager);
  EXPECT_EQ(registry, EcdsSubscriptionRegistry::get(manager));
  auto invalid = registry->reserve("xdstp://invalid", EcdsSubscriptionRegistry::Sharing::Exclusive);
  EXPECT_EQ(invalid.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_OK(registry->reserve("test-config", EcdsSubscriptionRegistry::Sharing::Exclusive));
}

TEST(EcdsSubscriptionRegistryTest, ConflictIdentifiesTheExistingSubscriberKind) {
  auto registry = std::make_shared<EcdsSubscriptionRegistry>();
  auto bootstrap = registry->reserve("test-config", EcdsSubscriptionRegistry::Sharing::Exclusive);
  ASSERT_OK(bootstrap);
  auto filter = registry->reserve("test-config", EcdsSubscriptionRegistry::Sharing::Shared);
  EXPECT_THAT(filter.status().message(), testing::HasSubstr("bootstrap module subscription"));
  bootstrap->reset();
  filter = registry->reserve("test-config", EcdsSubscriptionRegistry::Sharing::Shared);
  ASSERT_OK(filter);
  bootstrap = registry->reserve("test-config", EcdsSubscriptionRegistry::Sharing::Exclusive);
  EXPECT_THAT(bootstrap.status().message(), testing::HasSubstr("filter subscription"));
}

} // namespace
} // namespace Config
} // namespace Envoy
