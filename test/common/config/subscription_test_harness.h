#pragma once

#include "common/config/utility.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {

const uint64_t TEST_TIME_MILLIS = 42000;

/**
 * Interface for different Subscription implementation test harnesses. This has common functionality
 * that we can use to write tests that work across all Subscription types. EDS is used as the API in
 * tests depending on SubscriptionTestHarness, as representative of a subscription API.
 */
class SubscriptionTestHarness : public Event::TestUsingSimulatedTime {
public:
  SubscriptionTestHarness() : stats_(Utility::generateStats(stats_store_)) {
    simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS)));
  }
  virtual ~SubscriptionTestHarness() = default;

  /**
   * Start subscription and set related expectations.
   * @param cluster_names initial cluster names to request via EDS.
   */
  virtual void startSubscription(const std::set<std::string>& cluster_names) PURE;

  /**
   * Update cluster names to be delivered via EDS.
   * @param cluster_names cluster names.
   */
  virtual void updateResourceInterest(const std::set<std::string>& cluster_names) PURE;

  /**
   * Expect that an update request is sent by the Subscription implementation.
   * @param cluster_names cluster names to expect in the request.
   * @param version version_info to expect in the request.
   * @param expect_node whether the node information should be expected
   */
  virtual void expectSendMessage(const std::set<std::string>& cluster_names,
                                 const std::string& version, bool expect_node) PURE;

  /**
   * Deliver a response to the Subscription implementation and validate.
   * @param cluster_names cluster names to provide in the response
   * @param version version_info to provide in the response.
   * @param accept will the onConfigUpdate() callback accept the response?
   */
  virtual void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                                   const std::string& version, bool accept) PURE;

  virtual testing::AssertionResult statsAre(uint32_t attempt, uint32_t success, uint32_t rejected,
                                            uint32_t failure, uint32_t init_fetch_timeout,
                                            uint64_t update_time, uint64_t version) {
    // TODO(fredlas) rework update_success_ to make sense across all xDS carriers. Its value in
    // statsAre() calls in many tests will probably have to be changed.
    UNREFERENCED_PARAMETER(attempt);
    if (success != stats_.update_success_.value()) {
      return testing::AssertionFailure() << "update_success: expected " << success << ", got "
                                         << stats_.update_success_.value();
    }
    if (rejected != stats_.update_rejected_.value()) {
      return testing::AssertionFailure() << "update_rejected: expected " << rejected << ", got "
                                         << stats_.update_rejected_.value();
    }
    if (failure != stats_.update_failure_.value()) {
      return testing::AssertionFailure() << "update_failure: expected " << failure << ", got "
                                         << stats_.update_failure_.value();
    }
    if (init_fetch_timeout != stats_.init_fetch_timeout_.value()) {
      return testing::AssertionFailure() << "init_fetch_timeout: expected " << init_fetch_timeout
                                         << ", got " << stats_.init_fetch_timeout_.value();
    }
    if (update_time != stats_.update_time_.value()) {
      return testing::AssertionFailure()
             << "update_time: expected " << update_time << ", got " << stats_.update_time_.value();
    }
    if (version != stats_.version_.value()) {
      return testing::AssertionFailure()
             << "version: expected " << version << ", got " << stats_.version_.value();
    }
    return testing::AssertionSuccess();
  }

  virtual void verifyControlPlaneStats(uint32_t connected_state) {
    EXPECT_EQ(
        connected_state,
        stats_store_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)
            .value());
  }

  virtual void expectConfigUpdateFailed() PURE;

  virtual void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) PURE;

  virtual void expectDisableInitFetchTimeoutTimer() PURE;

  virtual void callInitFetchTimeoutCb() PURE;

  virtual void doSubscriptionTearDown() {}

  Stats::IsolatedStoreImpl stats_store_;
  SubscriptionStats stats_;
};

ACTION_P(ThrowOnRejectedConfig, accept) {
  if (!accept) {
    throw EnvoyException("bad config");
  }
}

} // namespace Config
} // namespace Envoy
