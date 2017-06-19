#include <fstream>

#include "common/event/dispatcher_impl.h"
#include "common/filesystem/subscription_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Filesystem {
namespace {

typedef SubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> EdsSubscriptionImpl;

class FilesystemSubscriptionImplTest : public testing::Test {
public:
  FilesystemSubscriptionImplTest()
      : path_(TestEnvironment::temporaryPath("eds.pb")), subscription_(dispatcher_, path_) {}

  void startSubscription(const std::vector<std::string>& cluster_names) {
    subscription_.start(cluster_names, callbacks_);
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

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) {
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
                        response_pb)))).WillOnce(Return(accept));
    updateFile(file_json);
  }

  const std::string path_;
  Event::DispatcherImpl dispatcher_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  EdsSubscriptionImpl subscription_;
};

// Validate basic inotify succeeds.
TEST_F(FilesystemSubscriptionImplTest, Basic) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that the client can reject a config.
TEST_F(FilesystemSubscriptionImplTest, RejectConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
}

// Validate that multiple updates succeed.
TEST_F(FilesystemSubscriptionImplTest, MultipleUpdates) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
}

// Validate that the client can reject a config and accept the same config later.
TEST_F(FilesystemSubscriptionImplTest, RejectAcceptConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that the client can reject a config and accept another config later.
TEST_F(FilesystemSubscriptionImplTest, RejectAcceptNextConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
}

// Validate that updateResources is a nop in the inotify implementation.
TEST_F(FilesystemSubscriptionImplTest, UpdateResources) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  subscription_.updateResources({"cluster2"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
}

// Validate that the client can recover from bad JSON responses.
TEST_F(FilesystemSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  updateFile(";!@#badjso n");
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
