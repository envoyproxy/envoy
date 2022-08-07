#pragma once

#include <fstream>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/filesystem_subscription_impl.h"
#include "source/common/config/utility.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::NiceMock;

namespace Envoy {
namespace Config {

class FilesystemSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  FilesystemSubscriptionTestHarness()
      : path_(makePathConfigSource(TestEnvironment::temporaryPath("eds.json"))),
        api_(Api::createApiForTest(stats_store_, simTime())), dispatcher_(setupDispatcher()),
        resource_decoder_(std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
                              envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name")),
        subscription_(*dispatcher_, path_, callbacks_, resource_decoder_, stats_,
                      validation_visitor_, *api_) {}

  ~FilesystemSubscriptionTestHarness() override { TestEnvironment::removePath(path_.path()); }

  Event::DispatcherPtr setupDispatcher() {
    auto dispatcher = std::make_unique<Event::MockDispatcher>();
    EXPECT_CALL(*dispatcher, createFilesystemWatcher_()).WillOnce(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new Filesystem::MockWatcher();
      EXPECT_CALL(*mock_watcher, addWatch(path_.path(), Filesystem::Watcher::Events::MovedTo, _))
          .WillOnce(Invoke([this](absl::string_view, uint32_t,
                                  Filesystem::Watcher::OnChangedCb cb) { on_changed_cb_ = cb; }));
      return mock_watcher;
    }));
    return dispatcher;
  }

  void startSubscription(const std::set<std::string>& cluster_names) override {
    std::ifstream config_file(path_.path());
    file_at_start_ = config_file.good();
    subscription_.start(flattenResources(cluster_names));
  }

  void updateResourceInterest(const std::set<std::string>& cluster_names) override {
    subscription_.updateResourceInterest(flattenResources(cluster_names));
  }

  void updateFile(const std::string& json, bool run_dispatcher = true) {
    // Write JSON contents to file, rename to path_ and invoke on change callback
    const std::string temp_path = TestEnvironment::writeStringToFileForTest("eds.json.tmp", json);
    TestEnvironment::renameFile(temp_path, path_.path());
    if (run_dispatcher) {
      on_changed_cb_(Filesystem::Watcher::Events::MovedTo);
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
                   "envoy.config.endpoint.v3.ClusterLoadAssignment\",\"clusterName\":\"" +
                   cluster + "\"},";
    }
    file_json.pop_back();
    file_json += "]}";
    envoy::service::discovery::v3::DiscoveryResponse response_pb;
    TestUtility::loadFromJson(file_json, response_pb);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::endpoint::v3::ClusterLoadAssignment>(
            response_pb, "cluster_name");
    EXPECT_CALL(callbacks_, onConfigUpdate(DecodedResourcesEq(decoded_resources.refvec_), version))
        .WillOnce(ThrowOnRejectedConfig(accept));
    if (accept) {
      version_ = version;
    } else {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
    }
    updateFile(file_json);
  }

  AssertionResult statsAre(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                           uint32_t init_fetch_timeout, uint64_t update_time, uint64_t version,
                           absl::string_view version_text) override {
    // The first attempt always fail unless there was a file there to begin with.
    return SubscriptionTestHarness::statsAre(attempt, success, rejected,
                                             failure + (file_at_start_ ? 0 : 1), init_fetch_timeout,
                                             update_time, version, version_text);
  }

  void expectConfigUpdateFailed() override { stats_.update_failure_.inc(); }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds) override {
    // initial_fetch_timeout not implemented.
  }

  void expectDisableInitFetchTimeoutTimer() override {
    // initial_fetch_timeout not implemented
  }

  void callInitFetchTimeoutCb() override {
    // initial_fetch_timeout not implemented
  }

  const envoy::config::core::v3::PathConfigSource path_;
  std::string version_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Filesystem::Watcher::OnChangedCb on_changed_cb_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  FilesystemSubscriptionImpl subscription_;
  bool file_at_start_{false};
};

} // namespace Config
} // namespace Envoy
