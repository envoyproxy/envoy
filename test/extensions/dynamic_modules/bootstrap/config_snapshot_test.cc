#include <thread>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/matchers.h"
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
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  EXPECT_EQ(calls, 1);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(json.value(), testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_THAT(json.value(), testing::Not(testing::HasSubstr("EndpointsConfigDump")));
  auto empty =
      ConfigSnapshot::collect(tracker, cluster_manager_, {}, Matchers::UniversalStringMatcher{});
  ASSERT_OK(empty);
  EXPECT_EQ(calls, 1);
  auto empty_json = empty.value()->serialize();
  ASSERT_OK(empty_json);
  EXPECT_EQ(empty_json.value(), "{}");
}

TEST_F(ConfigSnapshotTest, EncodesOnWorkerAndRedactsSecrets) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add("bootstrap", [](const Matchers::StringMatcher&) {
    auto dump = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
    (*dump->mutable_bootstrap()->mutable_node()->mutable_metadata()->mutable_fields())["opaque"]
        .set_string_value("application-metadata");
    auto* secret = dump->mutable_bootstrap()->mutable_static_resources()->add_secrets();
    secret->set_name("referenced-secret");
    secret->mutable_tls_certificate()->mutable_private_key()->set_inline_string("private-material");
    return dump;
  });
  const std::vector<absl::string_view> providers{"bootstrap"};
  auto collected = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                           Matchers::UniversalStringMatcher{});
  ASSERT_OK(collected);
  std::string json;
  std::thread worker([snapshot = std::move(collected.value()), &json]() {
    auto result = snapshot->serialize();
    EXPECT_OK(result);
    if (result.ok()) {
      json = std::move(result.value());
    }
  });
  worker.join();
  EXPECT_THAT(json, testing::HasSubstr("referenced-secret"));
  EXPECT_THAT(json, testing::HasSubstr("application-metadata"));
  EXPECT_THAT(json, testing::Not(testing::HasSubstr("private-material")));
}

TEST_F(ConfigSnapshotTest, ReportsProviderFailure) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add(
      "failed", [](const Matchers::StringMatcher&) { return ProtobufTypes::MessagePtr{}; });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_FALSE(ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                       Matchers::UniversalStringMatcher{})
                   .ok());
}

TEST_F(ConfigSnapshotTest, ProviderCanUnregisterDuringCollection) {
  Server::ConfigTrackerImpl tracker;
  Server::ConfigTracker::EntryOwnerPtr owner;
  int calls = 0;
  owner = tracker.add("bootstrap", [&owner, &calls](const Matchers::StringMatcher&) {
    owner.reset();
    EXPECT_EQ(++calls, 1);
    return std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  });
  const std::vector<absl::string_view> providers{"bootstrap"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_TRUE(tracker.getCallbacksMap().empty());
}

TEST_F(ConfigSnapshotTest, OrdersProvidersAndPassesNameMatcher) {
  Server::ConfigTrackerImpl tracker;
  auto clusters = tracker.add("clusters", [](const Matchers::StringMatcher& matcher) {
    auto dump = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
    for (absl::string_view name : {"selected", "excluded"}) {
      if (matcher.match(name)) {
        envoy::config::cluster::v3::Cluster cluster;
        cluster.set_name(name);
        EXPECT_TRUE(dump->add_static_clusters()->mutable_cluster()->PackFrom(cluster));
      }
    }
    return dump;
  });
  auto listeners = tracker.add("listeners", [](const Matchers::StringMatcher&) {
    return std::make_unique<envoy::admin::v3::ListenersConfigDump>();
  });
  const std::vector<absl::string_view> providers{"listeners", "clusters", "clusters"};
  auto snapshot =
      ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                              Matchers::StringMatcherImpl::createExactMatcher("selected"));
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("\"static_clusters\""));
  EXPECT_THAT(*json, testing::HasSubstr("selected"));
  EXPECT_THAT(*json, testing::Not(testing::HasSubstr("excluded")));
  const auto clusters_pos = json->find("ClustersConfigDump");
  const auto listeners_pos = json->find("ListenersConfigDump");
  ASSERT_NE(clusters_pos, std::string::npos);
  ASSERT_NE(listeners_pos, std::string::npos);
  EXPECT_LT(clusters_pos, listeners_pos);
  EXPECT_EQ(json->find("ClustersConfigDump", clusters_pos + 1), std::string::npos);
}

TEST_F(ConfigSnapshotTest, SkipsProviderUnregisteredDuringCollection) {
  Server::ConfigTrackerImpl tracker;
  auto removed = tracker.add("removed", [](const Matchers::StringMatcher&) {
    ADD_FAILURE() << "unregistered provider called";
    return std::make_unique<envoy::admin::v3::ClustersConfigDump>();
  });
  auto bootstrap = tracker.add("bootstrap", [&](const Matchers::StringMatcher&) {
    removed.reset();
    return std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  });
  const std::vector<absl::string_view> providers{"bootstrap", "removed"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_THAT(*json, testing::Not(testing::HasSubstr("ClustersConfigDump")));
}

TEST_F(ConfigSnapshotTest, UsesReplacementProviderDuringCollection) {
  Server::ConfigTrackerImpl tracker;
  auto replaced = tracker.add("replaced", [](const Matchers::StringMatcher&) {
    ADD_FAILURE() << "replaced provider called";
    return std::make_unique<envoy::admin::v3::ClustersConfigDump>();
  });
  auto bootstrap = tracker.add("bootstrap", [&](const Matchers::StringMatcher&) {
    replaced.reset();
    replaced = tracker.add("replaced", [](const Matchers::StringMatcher&) {
      return std::make_unique<envoy::admin::v3::ListenersConfigDump>();
    });
    return std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  });
  const std::vector<absl::string_view> providers{"bootstrap", "replaced"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("ListenersConfigDump"));
  EXPECT_THAT(*json, testing::Not(testing::HasSubstr("ClustersConfigDump")));
}

TEST_F(ConfigSnapshotTest, FiltersEndpointAssignmentsBeforeReadingHosts) {
  Server::ConfigTrackerImpl tracker;
  testing::NiceMock<Upstream::MockClusterMockPrioritySet> selected;
  selected.info_->eds_service_name_ = "selected-service";

  testing::NiceMock<Upstream::MockClusterMockPrioritySet> excluded;
  excluded.info_->name_ = "excluded-cluster";
  auto excluded_host = std::make_shared<testing::StrictMock<Upstream::MockHost>>();
  excluded.priority_set_.getMockHostSet(0)->hosts_.push_back(excluded_host);

  EXPECT_CALL(cluster_manager_, hasActiveClusters()).WillOnce(testing::Return(true));
  EXPECT_CALL(cluster_manager_, forEachActiveCluster(testing::_))
      .WillOnce([&](std::function<void(const Upstream::Cluster&)> callback) {
        callback(selected);
        callback(excluded);
      });
  const std::vector<absl::string_view> providers{"endpoint", "endpoint"};
  auto snapshot =
      ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                              Matchers::StringMatcherImpl::createExactMatcher("selected-service"));
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("selected-service"));
  EXPECT_THAT(*json, testing::Not(testing::HasSubstr("excluded-cluster")));
}

TEST_F(ConfigSnapshotTest, OmitsEndpointsWithoutActiveClusters) {
  Server::ConfigTrackerImpl tracker;
  EXPECT_CALL(cluster_manager_, hasActiveClusters()).WillOnce(testing::Return(false));
  const std::vector<absl::string_view> providers{"endpoint"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_EQ(*json, "{}");
}

TEST_F(ConfigSnapshotTest, UsesRegisteredEndpointProvider) {
  Server::ConfigTrackerImpl tracker;
  auto endpoint = tracker.add("endpoint", [](const Matchers::StringMatcher&) {
    return std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  });
  const std::vector<absl::string_view> providers{"endpoint"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("EndpointsConfigDump"));
}

TEST_F(ConfigSnapshotTest, SkipsRegisteredEndpointProviderUnregisteredDuringCollection) {
  Server::ConfigTrackerImpl tracker;
  auto endpoint = tracker.add("endpoint", [](const Matchers::StringMatcher&) {
    ADD_FAILURE() << "unregistered endpoint provider called";
    return std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  });
  auto bootstrap = tracker.add("bootstrap", [&](const Matchers::StringMatcher&) {
    endpoint.reset();
    return std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  });
  const std::vector<absl::string_view> providers{"endpoint", "bootstrap"};
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_THAT(*json, testing::Not(testing::HasSubstr("EndpointsConfigDump")));
}

TEST_F(ConfigSnapshotTest, ContainsProviderExceptions) {
  Server::ConfigTrackerImpl tracker;
  auto owner =
      tracker.add("failed", [](const Matchers::StringMatcher&) -> ProtobufTypes::MessagePtr {
        throw EnvoyException("provider failed");
      });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_THAT(ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                      Matchers::UniversalStringMatcher{})
                  .status()
                  .message(),
              testing::HasSubstr("provider failed"));
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
  auto snapshot = ConfigSnapshot::collect(tracker, cluster_manager_, providers,
                                          Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  EXPECT_FALSE(snapshot.value()->serialize().ok());
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
