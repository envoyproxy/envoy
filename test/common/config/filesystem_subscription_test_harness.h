#include <fstream>

#include "common/config/filesystem_subscription_impl.h"
#include "common/event/dispatcher_impl.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Config {

typedef FilesystemSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment>
    FilesystemEdsSubscriptionImpl;

class FilesystemSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  FilesystemSubscriptionTestHarness()
      : path_(TestEnvironment::temporaryPath("eds.pb")), subscription_(dispatcher_, path_) {}

  void startSubscription(const std::vector<std::string>& cluster_names) override {
    subscription_.start(cluster_names, callbacks_);
  }

  void updateResources(const std::vector<std::string>& cluster_names) override {
    UNREFERENCED_PARAMETER(cluster_names);
  }

  void updateFile(const std::string json) {
    // Write JSON contents to file, rename to path_ and run dispatcher to catch
    // inotify.
    const std::string temp_path = path_ + ".tmp";
    std::ofstream temp_file(temp_path);
    temp_file << json;
    temp_file.close();
    EXPECT_EQ(0, ::rename(temp_path.c_str(), path_.c_str()));
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) override {
    UNREFERENCED_PARAMETER(cluster_names);
    UNREFERENCED_PARAMETER(version);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) override {
    std::string file_json = "{\"versionInfo\":\"" + version + "\",\"resources\":[";
    for (const auto& cluster : cluster_names) {
      file_json += "{\"@type\":\"type.googleapis.com/"
                   "envoy.api.v2.ClusterLoadAssignment\",\"clusterName\":\"" +
                   cluster + "\"},";
    }
    file_json.pop_back();
    file_json += "]}";
    envoy::api::v2::DiscoveryResponse response_pb;
    EXPECT_EQ(google::protobuf::util::Status::OK,
              google::protobuf::util::JsonStringToMessage(file_json, &response_pb));
    EXPECT_CALL(callbacks_,
                onConfigUpdate(RepeatedProtoEq(
                    Config::Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(
                        response_pb)))).WillOnce(ThrowOnRejectedConfig(accept));
    if (!accept) {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
    }
    updateFile(file_json);
  }

  const std::string path_;
  Event::DispatcherImpl dispatcher_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  FilesystemEdsSubscriptionImpl subscription_;
};

} // namespace Config
} // namespace Envoy
