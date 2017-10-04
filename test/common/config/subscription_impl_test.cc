#include "test/common/config/filesystem_subscription_test_harness.h"
#include "test/common/config/grpc_subscription_test_harness.h"
#include "test/common/config/http_subscription_test_harness.h"
#include "test/common/config/subscription_test_harness.h"

namespace Envoy {
namespace Config {
namespace {

enum class SubscriptionType {
  Grpc,
  Http,
  Filesystem,
};

class SubscriptionImplTest : public testing::TestWithParam<SubscriptionType> {
public:
  SubscriptionImplTest() {
    switch (GetParam()) {
    case SubscriptionType::Grpc:
      test_harness_.reset(new GrpcSubscriptionTestHarness());
      break;
    case SubscriptionType::Http:
      test_harness_.reset(new HttpSubscriptionTestHarness());
      break;
    case SubscriptionType::Filesystem:
      test_harness_.reset(new FilesystemSubscriptionTestHarness());
      break;
    }
  }

  void startSubscription(const std::vector<std::string>& cluster_names) {
    test_harness_->startSubscription(cluster_names);
  }

  void updateResources(const std::vector<std::string>& cluster_names) {
    test_harness_->updateResources(cluster_names);
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) {
    test_harness_->expectSendMessage(cluster_names, version);
  }

  void verifyStats(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                   uint64_t version) {
    test_harness_->verifyStats(attempt, success, rejected, failure, version);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) {
    test_harness_->deliverConfigUpdate(cluster_names, version, accept);
  }

  std::unique_ptr<SubscriptionTestHarness> test_harness_;
};

INSTANTIATE_TEST_CASE_P(SubscriptionImplTest, SubscriptionImplTest,
                        testing::ValuesIn({SubscriptionType::Grpc, SubscriptionType::Http,
                                           SubscriptionType::Filesystem}));

// Validate basic request-response succeeds.
TEST_P(SubscriptionImplTest, InitialRequestResponse) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(2, 1, 0, 0, 7148434200721666028);
}

// Validate that multiple streamed updates succeed.
TEST_P(SubscriptionImplTest, ResponseStream) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(2, 1, 0, 0, 7148434200721666028);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  verifyStats(3, 2, 0, 0, 13237225503670494420U);
}

// Validate that the client can reject a config.
TEST_P(SubscriptionImplTest, RejectConfig) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  verifyStats(2, 0, 1, 0, 0);
}

// Validate that the client can reject a config and accept the same config later.
TEST_P(SubscriptionImplTest, RejectAcceptConfig) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  verifyStats(2, 0, 1, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(3, 1, 1, 0, 7148434200721666028);
}

// Validate that the client can reject a config and accept another config later.
TEST_P(SubscriptionImplTest, RejectAcceptNextConfig) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  verifyStats(2, 0, 1, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  verifyStats(3, 1, 1, 0, 13237225503670494420U);
}

// Validate that stream updates send a message with the updated resources.
TEST_P(SubscriptionImplTest, UpdateResources) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(2, 1, 0, 0, 7148434200721666028);
  updateResources({"cluster2"});
  verifyStats(3, 1, 0, 0, 7148434200721666028);
}

} // namespace
} // namespace Config
} // namespace Envoy
