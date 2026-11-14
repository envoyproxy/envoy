#include <thread>

#include "envoy/admin/v3/config_dump.pb.h"

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
  auto snapshot = ConfigSnapshot::collect(tracker, providers);
  ASSERT_OK(snapshot);
  EXPECT_EQ(calls, 1);
  auto json = snapshot.value()->serialize();
  ASSERT_OK(json);
  EXPECT_THAT(json.value(), testing::HasSubstr("BootstrapConfigDump"));
  EXPECT_THAT(json.value(), testing::Not(testing::HasSubstr("EndpointsConfigDump")));
  auto empty = ConfigSnapshot::collect(tracker, {});
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
      auto* secret = dump->mutable_bootstrap()->mutable_static_resources()->add_secrets();
      secret->set_name("referenced-secret");
      secret->mutable_tls_certificate()->mutable_private_key()->set_inline_string(
          "private-material");
      return dump;
    });
    const std::vector<absl::string_view> providers{"bootstrap"};
    auto collected = ConfigSnapshot::collect(tracker, providers);
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

TEST(ConfigSnapshotTest, ReportsProviderFailure) {
  Server::ConfigTrackerImpl tracker;
  auto owner = tracker.add(
      "failed", [](const Matchers::StringMatcher&) { return ProtobufTypes::MessagePtr{}; });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_FALSE(ConfigSnapshot::collect(tracker, providers).ok());
}

TEST(ConfigSnapshotTest, ContainsProviderExceptions) {
  Server::ConfigTrackerImpl tracker;
  auto owner =
      tracker.add("failed", [](const Matchers::StringMatcher&) -> ProtobufTypes::MessagePtr {
        throw EnvoyException("provider failed");
      });
  const std::vector<absl::string_view> providers{"failed"};
  EXPECT_FALSE(ConfigSnapshot::collect(tracker, providers).ok());
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
  auto snapshot = ConfigSnapshot::collect(tracker, providers);
  ASSERT_OK(snapshot);
  EXPECT_FALSE(snapshot.value()->serialize().ok());
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
