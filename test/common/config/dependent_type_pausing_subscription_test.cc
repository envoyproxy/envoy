#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"

#include "source/common/common/cleanup.h"
#include "source/common/config/dependent_type_pausing_subscription.h"
#include "source/common/config/dependent_type_urls.h"
#include "source/common/config/resource_name.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/config/xds_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

using testing::Eq;
using testing::Invoke;
using testing::Matcher;
using testing::Ref;
using testing::StrictMock;

class DependentTypePausingSubscriptionTest : public testing::Test {
public:
  // Creates a subscription for the given type URL, capturing the callbacks handed to the
  // underlying subscription.
  SubscriptionPtr createSubscription(absl::string_view type_url) {
    auto subscription_or_error = DependentTypePausingSubscription::create(
        xds_manager_, type_url, callbacks_, [this](SubscriptionCallbacks& callbacks) {
          inner_callbacks_ = &callbacks;
          auto subscription = std::make_unique<StrictMock<MockSubscription>>();
          inner_subscription_ = subscription.get();
          return absl::StatusOr<SubscriptionPtr>(std::move(subscription));
        });
    EXPECT_TRUE(subscription_or_error.ok());
    return std::move(subscription_or_error.value());
  }

  // Expects a pause of the given type URLs, which is lifted when the returned ScopedResume is
  // destroyed. paused_ reflects whether the pause is currently active.
  void expectPause(const std::vector<std::string>& type_urls) {
    EXPECT_CALL(xds_manager_, pause(Matcher<const std::vector<std::string>&>(Eq(type_urls))))
        .WillOnce(Invoke([this](const std::vector<std::string>&) -> ScopedResume {
          paused_ = true;
          return std::make_unique<Cleanup>([this]() { paused_ = false; });
        }));
  }

  StrictMock<MockXdsManager> xds_manager_;
  StrictMock<MockSubscriptionCallbacks> callbacks_;
  SubscriptionCallbacks* inner_callbacks_{};
  MockSubscription* inner_subscription_{};
  bool paused_{};
};

// Type URLs without dependent types are not wrapped.
TEST_F(DependentTypePausingSubscriptionTest, NoDependentTypeUrls) {
  const auto type_url = getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>();
  ASSERT_TRUE(dependentTypeUrls(type_url).empty());

  SubscriptionPtr subscription = createSubscription(type_url);
  EXPECT_EQ(inner_callbacks_, &callbacks_);
  EXPECT_EQ(subscription.get(), inner_subscription_);
}

// Errors creating the underlying subscription are propagated.
TEST_F(DependentTypePausingSubscriptionTest, CreationError) {
  const auto type_url = getTypeUrl<envoy::config::listener::v3::Listener>();
  auto subscription_or_error = DependentTypePausingSubscription::create(
      xds_manager_, type_url, callbacks_, [](SubscriptionCallbacks&) {
        return absl::StatusOr<SubscriptionPtr>(absl::InvalidArgumentError("bad config"));
      });
  EXPECT_EQ(subscription_or_error.status(), absl::InvalidArgumentError("bad config"));
}

// Subscription methods are forwarded to the underlying subscription.
TEST_F(DependentTypePausingSubscriptionTest, ForwardsSubscriptionMethods) {
  SubscriptionPtr subscription =
      createSubscription(getTypeUrl<envoy::config::listener::v3::Listener>());
  ASSERT_NE(inner_callbacks_, &callbacks_);
  ASSERT_NE(subscription.get(), inner_subscription_);

  const absl::flat_hash_set<std::string> names{"foo"};
  EXPECT_CALL(*inner_subscription_, start(names));
  subscription->start(names);
  EXPECT_CALL(*inner_subscription_, updateResourceInterest(names));
  subscription->updateResourceInterest(names);
  EXPECT_CALL(*inner_subscription_, requestOnDemandUpdate(names));
  subscription->requestOnDemandUpdate(names);
  EXPECT_CALL(*inner_subscription_, accept(names));
  subscription->accept(names);
}

// State-of-the-world updates are applied while the dependent types are paused.
TEST_F(DependentTypePausingSubscriptionTest, SotwUpdatePausesDependentTypes) {
  const auto type_url = getTypeUrl<envoy::config::listener::v3::Listener>();
  SubscriptionPtr subscription = createSubscription(type_url);

  const std::vector<DecodedResourceRef> resources;
  expectPause(dependentTypeUrls(type_url));
  EXPECT_CALL(callbacks_, onConfigUpdate(Ref(resources), "v1"))
      .WillOnce(Invoke([this](const std::vector<DecodedResourceRef>&, const std::string&) {
        EXPECT_TRUE(paused_);
        return absl::InvalidArgumentError("rejected");
      }));
  EXPECT_EQ(inner_callbacks_->onConfigUpdate(resources, "v1"),
            absl::InvalidArgumentError("rejected"));
  EXPECT_FALSE(paused_);
}

// Delta updates are applied while the dependent types are paused.
TEST_F(DependentTypePausingSubscriptionTest, DeltaUpdatePausesDependentTypes) {
  const auto type_url = getTypeUrl<envoy::config::listener::v3::Listener>();
  SubscriptionPtr subscription = createSubscription(type_url);

  const std::vector<DecodedResourceRef> added_resources;
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  expectPause(dependentTypeUrls(type_url));
  EXPECT_CALL(callbacks_, onConfigUpdate(Ref(added_resources), Ref(removed_resources), "v1"))
      .WillOnce(Invoke([this](const std::vector<DecodedResourceRef>&,
                              const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
        EXPECT_TRUE(paused_);
        return absl::OkStatus();
      }));
  EXPECT_TRUE(inner_callbacks_->onConfigUpdate(added_resources, removed_resources, "v1").ok());
  EXPECT_FALSE(paused_);
}

// Update failures are forwarded without pausing.
TEST_F(DependentTypePausingSubscriptionTest, ForwardsUpdateFailures) {
  SubscriptionPtr subscription =
      createSubscription(getTypeUrl<envoy::config::listener::v3::Listener>());

  EXPECT_CALL(callbacks_, onConfigUpdateFailed(ConfigUpdateFailureReason::FetchTimedout, nullptr));
  inner_callbacks_->onConfigUpdateFailed(ConfigUpdateFailureReason::FetchTimedout, nullptr);
}

} // namespace
} // namespace Config
} // namespace Envoy
