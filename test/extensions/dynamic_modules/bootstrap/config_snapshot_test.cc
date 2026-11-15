#include <thread>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/matchers.h"
#include "source/extensions/bootstrap/dynamic_modules/config_snapshot.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {
namespace {

TEST(ConfigSnapshotTest, SelectsProvidersOnceWithoutTraversingEndpoints) {
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
  auto snapshot = ConfigSnapshot::collect(tracker, providers, Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  EXPECT_EQ(calls, 1);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(json.value(), testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_THAT(json.value(), testing::Not(testing::HasSubstr("EndpointsConfigDump")));
  auto empty = ConfigSnapshot::collect(tracker, {}, Matchers::UniversalStringMatcher{});
  ASSERT_OK(empty);
  EXPECT_EQ(calls, 1);
  auto empty_json = empty.value()->serialize();
  ASSERT_OK(empty_json);
  EXPECT_EQ(empty_json.value(), "{}");
}

TEST(ConfigSnapshotTest, EncodesOnWorkerAfterTrackerDestructionAndRedactsSecrets) {
  std::unique_ptr<ConfigSnapshot> snapshot;
  {
    Server::ConfigTrackerImpl tracker;
    auto owner = tracker.add("bootstrap", [](const Matchers::StringMatcher&) {
      auto dump = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
      (*dump->mutable_bootstrap()->mutable_node()->mutable_metadata()->mutable_fields())["opaque"]
          .set_string_value("application-metadata");
      auto* secret = dump->mutable_bootstrap()->mutable_static_resources()->add_secrets();
      secret->set_name("referenced-secret");
      secret->mutable_tls_certificate()->mutable_private_key()->set_inline_string(
          "private-material");
      return dump;
    });
    const std::vector<absl::string_view> providers{"bootstrap"};
    auto collected =
        ConfigSnapshot::collect(tracker, providers, Matchers::UniversalStringMatcher{});
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
  EXPECT_THAT(json, testing::HasSubstr("application-metadata"));
  EXPECT_THAT(json, testing::Not(testing::HasSubstr("private-material")));
}

TEST(ConfigSnapshotTest, ReportsProviderFailure) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add(
      "failed", [](const Matchers::StringMatcher&) { return ProtobufTypes::MessagePtr{}; });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_FALSE(
      ConfigSnapshot::collect(tracker, providers, Matchers::UniversalStringMatcher{}).ok());
}

TEST(ConfigSnapshotTest, ProviderCanUnregisterDuringCollection) {
  Server::ConfigTrackerImpl tracker;
  Server::ConfigTracker::EntryOwnerPtr owner;
  owner = tracker.add("bootstrap", [&owner](const Matchers::StringMatcher&) {
    owner.reset();
    return std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
  });
  const std::vector<absl::string_view> providers{"bootstrap"};
  auto snapshot = ConfigSnapshot::collect(tracker, providers, Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_TRUE(tracker.getCallbacksMap().empty());
}

TEST(ConfigSnapshotTest, OrdersProvidersAndPassesNameMatcher) {
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
  auto snapshot = ConfigSnapshot::collect(
      tracker, providers, Matchers::StringMatcherImpl::createExactMatcher("selected"));
  ASSERT_OK(snapshot);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(*json, testing::HasSubstr("selected"));
  EXPECT_THAT(*json, testing::Not(testing::HasSubstr("excluded")));
  const auto clusters_pos = json->find("ClustersConfigDump");
  const auto listeners_pos = json->find("ListenersConfigDump");
  ASSERT_NE(clusters_pos, std::string::npos);
  ASSERT_NE(listeners_pos, std::string::npos);
  EXPECT_LT(clusters_pos, listeners_pos);
  EXPECT_EQ(json->find("ClustersConfigDump", clusters_pos + 1), std::string::npos);
}

TEST(ConfigSnapshotTest, ContainsProviderExceptions) {
  Server::ConfigTrackerImpl tracker;
  auto owner =
      tracker.add("failed", [](const Matchers::StringMatcher&) -> ProtobufTypes::MessagePtr {
        throw EnvoyException("provider failed");
      });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_THAT(ConfigSnapshot::collect(tracker, providers, Matchers::UniversalStringMatcher{})
                  .status()
                  .message(),
              testing::HasSubstr("provider failed"));
}

TEST(ConfigSnapshotTest, ReportsJsonEncodingFailure) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add("bootstrap", [](const Matchers::StringMatcher&) {
    auto dump = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
    auto* extension = dump->mutable_bootstrap()->add_bootstrap_extensions();
    extension->set_name("unknown");
    extension->mutable_typed_config()->set_type_url("type.googleapis.com/unknown.Config");
    return dump;
  });
  const std::vector<absl::string_view> providers{"bootstrap"};
  auto snapshot = ConfigSnapshot::collect(tracker, providers, Matchers::UniversalStringMatcher{});
  ASSERT_OK(snapshot);
  EXPECT_FALSE(snapshot.value()->serialize().ok());
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
