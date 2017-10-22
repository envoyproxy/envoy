#pragma once

#include "common/config/utility.h"

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {

/**
 * Interface for different Subscription implementation test harnesses. This has common functionality
 * that we can use to write tests that work across all Subscription types. EDS is used as the API in
 * tests depending on SubscriptionTestHarness, as representative of a subscription API.
 */
class SubscriptionTestHarness {
public:
  SubscriptionTestHarness() : stats_(Utility::generateStats(stats_store_)) {}
  virtual ~SubscriptionTestHarness() {}

  /**
   * Start subscription and set related expectations.
   * @param cluster_names initial cluster names to request via EDS.
   */
  virtual void startSubscription(const std::vector<std::string>& cluster_names) PURE;

  /**
   * Update cluster names to be delivered via EDS.
   * @param cluster_names cluster names.
   */
  virtual void updateResources(const std::vector<std::string>& cluster_names) PURE;

  /**
   * Expect that an update request is sent by the Subscription implementation.
   * @param cluster_names cluster names to expect in the request.
   * @param version version_info to expect in the request.
   */
  virtual void expectSendMessage(const std::vector<std::string>& cluster_names,
                                 const std::string& version) PURE;

  /**
   * Deliver a response to the Subscription implementation and validate.
   * @param cluster_names cluster names to provide in the response
   * @param version version_info to provide in the response.
   * @param accept will the onConfigUpdate() callback accept the response?
   */
  virtual void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                                   const std::string& version, bool accept) PURE;

  virtual void verifyStats(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                           uint64_t version) {
    EXPECT_EQ(attempt, stats_.update_attempt_.value());
    EXPECT_EQ(success, stats_.update_success_.value());
    EXPECT_EQ(rejected, stats_.update_rejected_.value());
    EXPECT_EQ(failure, stats_.update_failure_.value());
    EXPECT_EQ(version, stats_.version_.value());
  }

  Stats::MockIsolatedStatsStore stats_store_;
  SubscriptionStats stats_;
};

ACTION_P(ThrowOnRejectedConfig, accept) {
  if (!accept) {
    throw EnvoyException("bad config");
  }
}

} // namespace Config
} // namespace Envoy
