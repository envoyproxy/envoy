#include <memory>

#include "test/common/config/delta_subscription_test_harness.h"
#include "test/common/config/filesystem_subscription_test_harness.h"
#include "test/common/config/grpc_subscription_test_harness.h"
#include "test/common/config/http_subscription_test_harness.h"
#include "test/common/config/subscription_test_harness.h"

using testing::InSequence;

namespace Envoy {
namespace Config {
namespace {

enum class SubscriptionType {
  Grpc,
  DeltaGrpc,
  Http,
  Filesystem,
};

class SubscriptionImplTest : public testing::TestWithParam<SubscriptionType> {
public:
  SubscriptionImplTest() : SubscriptionImplTest(std::chrono::milliseconds(0)) {}
  SubscriptionImplTest(std::chrono::milliseconds init_fetch_timeout) {
    switch (GetParam()) {
    case SubscriptionType::Grpc:
      test_harness_ = std::make_unique<GrpcSubscriptionTestHarness>(init_fetch_timeout);
      break;
    case SubscriptionType::DeltaGrpc:
      test_harness_ = std::make_unique<DeltaSubscriptionTestHarness>(init_fetch_timeout);
      break;
    case SubscriptionType::Http:
      test_harness_ = std::make_unique<HttpSubscriptionTestHarness>(init_fetch_timeout);
      break;
    case SubscriptionType::Filesystem:
      test_harness_ = std::make_unique<FilesystemSubscriptionTestHarness>();
      break;
    }
  }

  void startSubscription(const std::set<std::string>& cluster_names) {
    test_harness_->startSubscription(cluster_names);
  }

  void updateResources(const std::set<std::string>& cluster_names) {
    test_harness_->updateResources(cluster_names);
  }

  void expectSendMessage(const std::set<std::string>& cluster_names, const std::string& version) {
    test_harness_->expectSendMessage(cluster_names, version);
  }

  AssertionResult statsAre(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                           uint32_t init_fetch_timeout, uint64_t version) {
    return test_harness_->statsAre(attempt, success, rejected, failure, init_fetch_timeout,
                                   version);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) {
    test_harness_->deliverConfigUpdate(cluster_names, version, accept);
  }

  void expectConfigUpdateFailed() { test_harness_->expectConfigUpdateFailed(); }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) {
    test_harness_->expectEnableInitFetchTimeoutTimer(timeout);
  }

  void expectDisableInitFetchTimeoutTimer() { test_harness_->expectDisableInitFetchTimeoutTimer(); }

  void callInitFetchTimeoutCb() { test_harness_->callInitFetchTimeoutCb(); }

  std::unique_ptr<SubscriptionTestHarness> test_harness_;
};

class SubscriptionImplInitFetchTimeoutTest : public SubscriptionImplTest {
public:
  SubscriptionImplInitFetchTimeoutTest() : SubscriptionImplTest(std::chrono::milliseconds(1000)) {}
};

SubscriptionType types[] = {SubscriptionType::Grpc, SubscriptionType::DeltaGrpc,
                            SubscriptionType::Http, SubscriptionType::Filesystem};
INSTANTIATE_TEST_SUITE_P(SubscriptionImplTest, SubscriptionImplTest, testing::ValuesIn(types));
INSTANTIATE_TEST_SUITE_P(SubscriptionImplTest, SubscriptionImplInitFetchTimeoutTest,
                         testing::ValuesIn(types));

// Validate basic request-response succeeds.
TEST_P(SubscriptionImplTest, InitialRequestResponse) {
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  statsAre(2, 1, 0, 0, 0, 7148434200721666028);
}

// Validate that multiple streamed updates succeed.
TEST_P(SubscriptionImplTest, ResponseStream) {
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  statsAre(2, 1, 0, 0, 0, 7148434200721666028);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  statsAre(3, 2, 0, 0, 0, 13237225503670494420U);
}

// Validate that the client can reject a config.
TEST_P(SubscriptionImplTest, RejectConfig) {
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  statsAre(2, 0, 1, 0, 0, 0);
}

// Validate that the client can reject a config and accept the same config later.
TEST_P(SubscriptionImplTest, RejectAcceptConfig) {
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  statsAre(2, 0, 1, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  statsAre(3, 1, 1, 0, 0, 7148434200721666028);
}

// Validate that the client can reject a config and accept another config later.
TEST_P(SubscriptionImplTest, RejectAcceptNextConfig) {
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  statsAre(2, 0, 1, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  statsAre(3, 1, 1, 0, 0, 13237225503670494420U);
}

// Validate that stream updates send a message with the updated resources.
TEST_P(SubscriptionImplTest, UpdateResources) {
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  statsAre(2, 1, 0, 0, 0, 7148434200721666028);
  updateResources({"cluster2"});
  statsAre(3, 1, 0, 0, 0, 7148434200721666028);
}

// Validate that initial fetch timer is created and calls callback on timeout
TEST_P(SubscriptionImplInitFetchTimeoutTest, InitialFetchTimeout) {
  InSequence s;
  expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds(1000));
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  expectConfigUpdateFailed();
  callInitFetchTimeoutCb();
  statsAre(1, 0, 0, 0, 1, 0);
}

// Validate that initial fetch timer is disabled on config update
TEST_P(SubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnSuccess) {
  InSequence s;
  expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds(1000));
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  expectDisableInitFetchTimeoutTimer();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that initial fetch timer is disabled on config update failed
TEST_P(SubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnFail) {
  InSequence s;
  expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds(1000));
  startSubscription({"cluster0", "cluster1"});
  statsAre(1, 0, 0, 0, 0, 0);
  expectDisableInitFetchTimeoutTimer();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
}

} // namespace
} // namespace Config
} // namespace Envoy
