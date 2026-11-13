#include <thread>

#include "envoy/admin/v3/config_dump.pb.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/bootstrap/dynamic_modules/config_snapshot.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {
namespace {

class ConfigSnapshotTest : public testing::Test {
protected:
  testing::StrictMock<Upstream::MockClusterManager> cluster_manager_;
};

TEST_F(ConfigSnapshotTest, SelectsProvidersOnceWithoutTraversingEndpoints) {
  Server::ConfigTrackerImpl tracker;
  int calls = 0;
  auto bootstrap = tracker.add("bootstrap", [&calls](const Matchers::StringMatcher&) {
    ++calls;
    return std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  });
  auto endpoints = tracker.add("endpoint", [](const Matchers::StringMatcher&) {
    ADD_FAILURE() << "unselected endpoint provider called";
    return std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  });
  const std::vector<absl::string_view> providers{"bootstrap", "routes", "bootstrap"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers);
  ASSERT_OK(snapshot);
  EXPECT_EQ(calls, 1);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(json.value(), testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_THAT(json.value(), testing::Not(testing::HasSubstr("EndpointsConfigDump")));
  auto empty = ConfigSnapshot::collect(tracker, cluster_manager_, {});
  ASSERT_OK(empty);
  EXPECT_EQ(calls, 1);
  auto empty_json = empty.value()->serialize();
  ASSERT_OK(empty_json);
  EXPECT_EQ(empty_json.value(), "{}");
}

TEST_F(ConfigSnapshotTest, CollectsEndpointAssignmentsOnce) {
  Server::ConfigTrackerImpl tracker;
  testing::NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster.info_->eds_service_name_ = "eds-service";
  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(testing::Return(true));
  auto host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  const std::string hostname = "endpoint.example";
  const envoy::config::core::v3::Locality locality;
  auto address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.1", 80);
  ON_CALL(*host, hostname()).WillByDefault(testing::ReturnRef(hostname));
  ON_CALL(*host, hostnameForHealthChecks()).WillByDefault(testing::ReturnRef(hostname));
  ON_CALL(*host, locality()).WillByDefault(testing::ReturnRef(locality));
  ON_CALL(*host, address()).WillByDefault(testing::Return(address));
  ON_CALL(*host, healthCheckAddress()).WillByDefault(testing::Return(address));
  cluster.priority_set_.getMockHostSet(0)->hosts_.push_back(host);
  EXPECT_CALL(cluster_manager_, hasActiveClusters()).WillOnce(testing::Return(true));
  EXPECT_CALL(cluster_manager_, forEachActiveCluster(testing::_))
      .WillOnce([&](std::function<void(const Upstream::Cluster&)> callback) { callback(cluster); });
  const std::vector<absl::string_view> providers{"endpoint", "endpoint"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers);
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(json.value(), testing::HasSubstr("EndpointsConfigDump"));
  EXPECT_THAT(json.value(), testing::HasSubstr("dynamic_endpoint_configs"));
  EXPECT_THAT(json.value(), testing::HasSubstr("eds-service"));
  EXPECT_THAT(json.value(), testing::HasSubstr("192.0.2.1"));
  EXPECT_THAT(json.value(), testing::HasSubstr(hostname));
}

TEST_F(ConfigSnapshotTest, OmitsEndpointsWithoutActiveClusters) {
  Server::ConfigTrackerImpl tracker;
  EXPECT_CALL(cluster_manager_, hasActiveClusters()).WillOnce(testing::Return(false));
  const std::vector<absl::string_view> providers{"endpoint"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers);
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_EQ(json.value(), "{}");
}

TEST_F(ConfigSnapshotTest, UsesRegisteredEndpointProvider) {
  Server::ConfigTrackerImpl tracker;
  auto endpoint = tracker.add("endpoint", [](const Matchers::StringMatcher&) {
    return std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  });
  const std::vector<absl::string_view> providers{"endpoint"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers);
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(json.value(), testing::HasSubstr("EndpointsConfigDump"));
}

TEST_F(ConfigSnapshotTest, EncodesOnWorkerAfterTrackerDestructionAndRedactsSecrets) {
  std::unique_ptr<ConfigSnapshot> snapshot;
  {
    Server::ConfigTrackerImpl tracker;
    auto owner = tracker.add("bootstrap", [](const Matchers::StringMatcher&) {
      auto dump = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
      auto* secret = dump->mutable_bootstrap()->mutable_static_resources()->add_secrets();
      secret->set_name("referenced-secret");
      secret->mutable_tls_certificate()->mutable_private_key()->set_inline_string(
          "private-material");
      return dump;
    });
    const std::vector<absl::string_view> providers{"bootstrap"};
    auto collected = ConfigSnapshot::collect(tracker, cluster_manager_, providers);
    ASSERT_OK(collected);
    snapshot = std::move(collected.value());
  }
  std::string json;
  std::thread worker([snapshot = std::move(snapshot), &json]() {
    auto result = snapshot->serialize();
    EXPECT_OK(result);
    if (result.ok()) {
      json = std::move(result.value());
    }
  });
  worker.join();
  EXPECT_THAT(json, testing::HasSubstr("referenced-secret"));
  EXPECT_THAT(json, testing::Not(testing::HasSubstr("private-material")));
}

TEST_F(ConfigSnapshotTest, ReportsProviderFailure) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add(
      "failed", [](const Matchers::StringMatcher&) { return ProtobufTypes::MessagePtr{}; });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_FALSE(ConfigSnapshot::collect(tracker, cluster_manager_, providers).ok());
}

TEST_F(ConfigSnapshotTest, ContainsProviderExceptions) {
  Server::ConfigTrackerImpl tracker;
  auto owner =
      tracker.add("failed", [](const Matchers::StringMatcher&) -> ProtobufTypes::MessagePtr {
        throw EnvoyException("provider failed");
      });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_FALSE(ConfigSnapshot::collect(tracker, cluster_manager_, providers).ok());
}

TEST_F(ConfigSnapshotTest, ReportsJsonEncodingFailure) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add("bootstrap", [](const Matchers::StringMatcher&) {
    auto dump = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
    auto* extension = dump->mutable_bootstrap()->add_bootstrap_extensions();
    extension->set_name("unknown");
    extension->mutable_typed_config()->set_type_url("type.googleapis.com/unknown.Config");
    return dump;
  });
  const std::vector<absl::string_view> providers{"bootstrap"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers);
  ASSERT_OK(snapshot);
  EXPECT_FALSE(snapshot.value()->serialize().ok());
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
