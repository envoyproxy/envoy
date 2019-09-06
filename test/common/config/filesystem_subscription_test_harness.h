#pragma once

#include <fstream>

#include "envoy/api/v2/eds.pb.h"

#include "common/config/filesystem_subscription_impl.h"
#include "common/config/utility.h"
#include "common/event/dispatcher_impl.h"
#include "common/protobuf/utility.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Config {

class FilesystemSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  FilesystemSubscriptionTestHarness()
      : path_(TestEnvironment::temporaryPath("eds.json")),
        api_(Api::createApiForTest(stats_store_)), dispatcher_(api_->allocateDispatcher()),
        subscription_(*dispatcher_, path_, callbacks_, stats_, validation_visitor_, *api_) {}

  ~FilesystemSubscriptionTestHarness() override {
    if (::access(path_.c_str(), F_OK) != -1) {
      EXPECT_EQ(0, ::unlink(path_.c_str()));
    }
  }

  void startSubscription(const std::set<std::string>& cluster_names) override {
    std::ifstream config_file(path_);
    file_at_start_ = config_file.good();
    subscription_.start(cluster_names);
  }

  void updateResources(const std::set<std::string>& cluster_names) override {
    subscription_.updateResources(cluster_names);
  }

  void updateFile(const std::string json, bool run_dispatcher = true) {
    // Write JSON contents to file, rename to path_ and run dispatcher to catch
    // inotify.
    const std::string temp_path = TestEnvironment::writeStringToFileForTest("eds.json.tmp", json);
    TestUtility::renameFile(temp_path, path_);
    if (run_dispatcher) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void expectSendMessage(const std::set<std::string>& cluster_names, const std::string& version,
                         bool expect_node) override {
    UNREFERENCED_PARAMETER(cluster_names);
    UNREFERENCED_PARAMETER(version);
    UNREFERENCED_PARAMETER(expect_node);
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept) override {
    std::string file_json = "{\"versionInfo\":\"" + version + "\",\"resources\":[";
    for (const auto& cluster : cluster_names) {
      file_json += "{\"@type\":\"type.googleapis.com/"
                   "envoy.api.v2.ClusterLoadAssignment\",\"clusterName\":\"" +
                   cluster + "\"},";
    }
    file_json.pop_back();
    file_json += "]}";
    envoy::api::v2::DiscoveryResponse response_pb;
    TestUtility::loadFromJson(file_json, response_pb);
    EXPECT_CALL(callbacks_, onConfigUpdate(RepeatedProtoEq(response_pb.resources()), version))
        .WillOnce(ThrowOnRejectedConfig(accept));
    if (accept) {
      version_ = version;
    } else {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
    }
    updateFile(file_json);
  }

  AssertionResult statsAre(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                           uint32_t init_fetch_timeout, uint64_t version) override {
    // The first attempt always fail unless there was a file there to begin with.
    return SubscriptionTestHarness::statsAre(attempt, success, rejected,
                                             failure + (file_at_start_ ? 0 : 1), init_fetch_timeout,
                                             version);
  }

  void expectConfigUpdateFailed() override {
    // initial_fetch_timeout not implemented
  }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) override {
    UNREFERENCED_PARAMETER(timeout);
    // initial_fetch_timeout not implemented
  }

  void expectDisableInitFetchTimeoutTimer() override {
    // initial_fetch_timeout not implemented
  }

  void callInitFetchTimeoutCb() override {
    // initial_fetch_timeout not implemented
  }

  const std::string path_;
  std::string version_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
  FilesystemSubscriptionImpl subscription_;
  bool file_at_start_{false};
};

} // namespace Config
} // namespace Envoy
