#include <memory>
#include <optional>
#include <string>

#include "source/common/runtime/breaking_changes.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "absl/flags/flag.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Runtime {
namespace {

class BreakingChangesTrackerTest : public testing::Test {
protected:
  void SetUp() override {
    filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request);
  }

  void TearDown() override {
    absl::SetFlag(&FLAGS_breaking_change_observability_enabled, false);
    absl::SetFlag(&FLAGS_envoy_reloadable_features_test_enabled_breaking_change, true);
    absl::SetFlag(&FLAGS_envoy_reloadable_features_test_disabled_breaking_change, false);
  }

  StreamInfo::FilterStateSharedPtr filter_state_;
};

TEST_F(BreakingChangesTrackerTest, DefaultState) {
  EXPECT_FALSE(BreakingChangesTracker::IsEnabled());

  BreakingChangesTracker tracker;
  EXPECT_EQ(0u, tracker.test_enabled_breaking_change);
  EXPECT_EQ(0u, tracker.test_disabled_breaking_change);
  EXPECT_EQ(std::nullopt, tracker.serializeAsString());
}

TEST_F(BreakingChangesTrackerTest, Serialization) {
  BreakingChangesTracker tracker;
  EXPECT_EQ(std::nullopt, tracker.serializeAsString());

  tracker.test_enabled_breaking_change = 1;
  EXPECT_EQ(std::optional<std::string>("test_enabled_breaking_change"),
            tracker.serializeAsString());

  tracker.test_enabled_breaking_change = 0;
  tracker.test_disabled_breaking_change = 1;
  EXPECT_EQ(std::optional<std::string>("test_disabled_breaking_change"),
            tracker.serializeAsString());

  tracker.test_enabled_breaking_change = 1;
  EXPECT_EQ(
      std::optional<std::string>("test_enabled_breaking_change,test_disabled_breaking_change"),
      tracker.serializeAsString());
}

TEST_F(BreakingChangesTrackerTest, FromFilterStateCreatesAndReusesObject) {
  EXPECT_FALSE(filter_state_->hasData<BreakingChangesTracker>(BreakingChangesTrackerDataName));

  BreakingChangesTracker& tracker1 = BreakingChangesTracker::fromFilterState(filter_state_);
  EXPECT_TRUE(filter_state_->hasData<BreakingChangesTracker>(BreakingChangesTrackerDataName));
  tracker1.test_enabled_breaking_change = 1;

  BreakingChangesTracker& tracker2 = BreakingChangesTracker::fromFilterState(filter_state_);
  EXPECT_EQ(&tracker1, &tracker2);
  EXPECT_EQ(1u, tracker2.test_enabled_breaking_change);
  EXPECT_EQ(std::optional<std::string>("test_enabled_breaking_change"),
            tracker2.serializeAsString());
}

TEST_F(BreakingChangesTrackerTest, ObservedBreakingChangeObservabilityDisabled) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, false);
  EXPECT_FALSE(BreakingChangesTracker::IsEnabled());

  OBSERVED_BREAKING_CHANGE(test_enabled_breaking_change, filter_state_);
  EXPECT_FALSE(filter_state_->hasData<BreakingChangesTracker>(BreakingChangesTrackerDataName));
}

TEST_F(BreakingChangesTrackerTest, ObservedBreakingChangeObservabilityEnabled) {
  absl::SetFlag(&FLAGS_breaking_change_observability_enabled, true);
  EXPECT_TRUE(BreakingChangesTracker::IsEnabled());

  OBSERVED_BREAKING_CHANGE(test_enabled_breaking_change, filter_state_);
  ASSERT_TRUE(filter_state_->hasData<BreakingChangesTracker>(BreakingChangesTrackerDataName));
  const auto* tracker =
      filter_state_->getDataReadOnly<BreakingChangesTracker>(BreakingChangesTrackerDataName);
  ASSERT_NE(nullptr, tracker);
  EXPECT_EQ(std::optional<std::string>("test_enabled_breaking_change"),
            tracker->serializeAsString());

  OBSERVED_BREAKING_CHANGE(test_disabled_breaking_change, filter_state_);
  EXPECT_EQ(
      std::optional<std::string>("test_enabled_breaking_change,test_disabled_breaking_change"),
      tracker->serializeAsString());
}

TEST_F(BreakingChangesTrackerTest, BreakingChangeEnabledMacro) {
  EXPECT_TRUE(BREAKING_CHANGE_ENABLED(test_enabled_breaking_change));
  EXPECT_FALSE(BREAKING_CHANGE_ENABLED(test_disabled_breaking_change));

  absl::SetFlag(&FLAGS_envoy_reloadable_features_test_enabled_breaking_change, false);
  absl::SetFlag(&FLAGS_envoy_reloadable_features_test_disabled_breaking_change, true);

  EXPECT_FALSE(BREAKING_CHANGE_ENABLED(test_enabled_breaking_change));
  EXPECT_TRUE(BREAKING_CHANGE_ENABLED(test_disabled_breaking_change));
}

} // namespace
} // namespace Runtime
} // namespace Envoy
