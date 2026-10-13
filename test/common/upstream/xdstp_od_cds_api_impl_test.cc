#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"

#include "source/common/config/decoded_resource_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/od_cds_api_impl.h"

#include "test/mocks/config/xds_manager.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/missing_cluster_notifier.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "fmt/core.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::UnorderedElementsAre;

class XdstpOdCdsApiImplTest : public testing::Test {
public:
  void SetUp() override {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.xdstp_based_config_singleton_subscriptions", "true"}});
    envoy::config::core::v3::ConfigSource odcds_config;
    OptRef<xds::core::v3::ResourceLocator> null_locator;
    odcds_ = *XdstpOdCdsApiImpl::create(odcds_config, null_locator, xds_manager_, cm_, notifier_,
                                        *store_.rootScope(), validation_visitor_,
                                        server_factory_context_);

    ON_CALL(xds_manager_, subscriptionFactory())
        .WillByDefault(ReturnRef(cm_.subscription_factory_));
  }

  void expectSingletonSubscription(absl::string_view resource_name) {
    EXPECT_CALL(xds_manager_, subscribeToSingletonResource(resource_name, _, _, _, _, _, _))
        .WillOnce(Invoke(
            [this](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
                   absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                   Config::OpaqueResourceDecoderSharedPtr,
                   const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              auto ret = std::make_unique<NiceMock<Config::MockSubscription>>();
              subscription_ = ret.get();
              odcds_callbacks_ = &callbacks;
              return ret;
            }));
  }

  TestScopedRuntime scoped_runtime_;
  NiceMock<Config::MockXdsManager> xds_manager_;
  NiceMock<MockClusterManager> cm_;
  Stats::IsolatedStoreImpl store_;
  MockMissingClusterNotifier notifier_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  OdCdsApiSharedPtr odcds_;
  Config::SubscriptionCallbacks* odcds_callbacks_ = nullptr;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Config::MockSubscription* subscription_;
};

// Check that a subscription is created when the odcds is updated.
TEST_F(XdstpOdCdsApiImplTest, SuccesfulSubscriptionToCluster) {
  InSequence s;

  expectSingletonSubscription("fake_cluster");
  odcds_->updateOnDemand("fake_cluster");
}

// Check that a subscription is created when the odcds is updated,
// and if the same cluster is requested again, another subscription will not be created.
TEST_F(XdstpOdCdsApiImplTest, SingleSubscriptionToSomeCluster) {
  InSequence s;

  expectSingletonSubscription("fake_cluster");
  odcds_->updateOnDemand("fake_cluster");

  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(_, _, _, _, _, _, _)).Times(0);
  odcds_->updateOnDemand("fake_cluster");
}

// Check that a subscription is created when the odcds is updated,
// and if a different cluster is requested, a new subscription will be created.
TEST_F(XdstpOdCdsApiImplTest, TwoSubscriptionsToDifferectClusters) {
  InSequence s;

  expectSingletonSubscription("fake_cluster");
  odcds_->updateOnDemand("fake_cluster");

  expectSingletonSubscription("fake_cluster2");
  odcds_->updateOnDemand("fake_cluster2");
}

// Tests a successful subscription and cluster addition.
TEST_F(XdstpOdCdsApiImplTest, SuccessfulClusterAddition) {
  InSequence s;

  const std::string cluster_name = "fake_cluster";
  expectSingletonSubscription(cluster_name);
  odcds_->updateOnDemand(cluster_name);

  ASSERT_NE(odcds_callbacks_, nullptr);

  const auto cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name));

  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));

  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), "fake_cluster", {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);

  EXPECT_OK(odcds_callbacks_->onConfigUpdate(resources, version));
}

// Tests cluster removal.
TEST_F(XdstpOdCdsApiImplTest, ClusterRemoval) {
  InSequence s;

  const std::string cluster_name = "fake_cluster";
  expectSingletonSubscription(cluster_name);
  odcds_->updateOnDemand(cluster_name);

  ASSERT_NE(odcds_callbacks_, nullptr);

  const auto cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name));

  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));

  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), "fake_cluster", {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);

  EXPECT_OK(odcds_callbacks_->onConfigUpdate(resources, version));

  // Now remove the cluster.
  const std::string version2 = "v2";
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name));
  EXPECT_OK(odcds_callbacks_->onConfigUpdate({}, version2));
}

// Tests that a config update failure is handled correctly.
TEST_F(XdstpOdCdsApiImplTest, SubscriptionFailure) {
  InSequence s;

  const std::string cluster_name = "fake_cluster";
  expectSingletonSubscription(cluster_name);
  odcds_->updateOnDemand(cluster_name);

  ASSERT_NE(odcds_callbacks_, nullptr);

  EXPECT_CALL(cm_, addOrUpdateCluster(_, _, _)).Times(0);
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name));

  EnvoyException e("rejecting update");
  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                         &e);
}

// Tests that a config update failure with a null exception pointer is handled correctly.
TEST_F(XdstpOdCdsApiImplTest, SubscriptionFailureNullException) {
  InSequence s;

  const std::string cluster_name = "fake_cluster";
  expectSingletonSubscription(cluster_name);
  odcds_->updateOnDemand(cluster_name);

  ASSERT_NE(odcds_callbacks_, nullptr);

  EXPECT_CALL(cm_, addOrUpdateCluster(_, _, _)).Times(0);
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name));

  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                         nullptr);
}

// Tests that an existing cluster is updated.
TEST_F(XdstpOdCdsApiImplTest, ClusterUpdate) {
  InSequence s;

  const std::string cluster_name = "fake_cluster";
  expectSingletonSubscription(cluster_name);
  odcds_->updateOnDemand(cluster_name);

  ASSERT_NE(odcds_callbacks_, nullptr);

  const auto cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name));

  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));

  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), "fake_cluster", {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);

  EXPECT_OK(odcds_callbacks_->onConfigUpdate(resources, version));

  // Now update the cluster.
  const auto updated_cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 2.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name));
  const std::string version2 = "v2";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(updated_cluster), version2, false));

  Config::DecodedResourceImpl decoded_resource2(
      std::make_unique<envoy::config::cluster::v3::Cluster>(updated_cluster), "fake_cluster", {},
      version2);
  std::vector<Config::DecodedResourceRef> resources2;
  resources2.emplace_back(decoded_resource2);

  EXPECT_OK(odcds_callbacks_->onConfigUpdate(resources2, version2));
}

// Tests that multiple subscriptions are handled independently.
TEST_F(XdstpOdCdsApiImplTest, MultipleSubscriptions) {
  InSequence s;

  // Subscription for fake_cluster1 succeeds.
  const std::string cluster_name1 = "fake_cluster1";
  Config::SubscriptionCallbacks* callbacks1 = nullptr;
  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(cluster_name1, _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&callbacks1](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
                        absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                        Config::OpaqueResourceDecoderSharedPtr, const Config::SubscriptionOptions&)
              -> absl::StatusOr<Config::SubscriptionPtr> {
            callbacks1 = &callbacks;
            return std::make_unique<NiceMock<Config::MockSubscription>>();
          }));
  odcds_->updateOnDemand(cluster_name1);
  ASSERT_NE(callbacks1, nullptr);

  // Subscription for fake_cluster2 fails.
  const std::string cluster_name2 = "fake_cluster2";
  Config::SubscriptionCallbacks* callbacks2 = nullptr;
  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(cluster_name2, _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&callbacks2](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
                        absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                        Config::OpaqueResourceDecoderSharedPtr, const Config::SubscriptionOptions&)
              -> absl::StatusOr<Config::SubscriptionPtr> {
            callbacks2 = &callbacks;
            return std::make_unique<NiceMock<Config::MockSubscription>>();
          }));
  odcds_->updateOnDemand(cluster_name2);
  ASSERT_NE(callbacks2, nullptr);

  // Verify that the successful subscription works as expected.
  const auto cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name1));
  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));
  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), cluster_name1, {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);
  EXPECT_OK(callbacks1->onConfigUpdate(resources, version));

  // Verify that the failed subscription works as expected.
  EXPECT_CALL(cm_, addOrUpdateCluster(_, _, _)).Times(0);
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name2));
  EnvoyException e("rejecting update");
  callbacks2->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
}

// Tests cluster removal via a delta update.
TEST_F(XdstpOdCdsApiImplTest, ClusterRemovalViaDeltaUpdate) {
  InSequence s;

  const std::string cluster_name = "fake_cluster";
  expectSingletonSubscription(cluster_name);
  odcds_->updateOnDemand(cluster_name);

  ASSERT_NE(odcds_callbacks_, nullptr);

  // First, add the cluster.
  const auto cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name));
  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));
  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), cluster_name, {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);
  EXPECT_OK(odcds_callbacks_->onConfigUpdate(resources, version));

  // Now, remove the cluster using a delta update.
  const std::string version2 = "v2";
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add(std::string(cluster_name));
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name));
  EXPECT_OK(odcds_callbacks_->onConfigUpdate({}, removed_resources, version2));
}

// Tests that an ADS config source is passed to the per-resource subscriptions.
TEST_F(XdstpOdCdsApiImplTest, AdsConfigSourceSubscription) {
  envoy::config::core::v3::ConfigSource ads_config_source;
  ads_config_source.mutable_ads();
  OptRef<xds::core::v3::ResourceLocator> null_locator;
  auto odcds =
      *XdstpOdCdsApiImpl::create(ads_config_source, null_locator, xds_manager_, cm_, notifier_,
                                 *store_.rootScope(), validation_visitor_, server_factory_context_);

  EXPECT_CALL(xds_manager_, subscribeToSingletonResource("fake_cluster", _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource> config_source,
              absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks&,
              Config::OpaqueResourceDecoderSharedPtr,
              const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
            EXPECT_TRUE(config_source.has_value());
            EXPECT_THAT(config_source.ref(), ProtoEq(ads_config_source));
            return std::make_unique<NiceMock<Config::MockSubscription>>();
          }));
  odcds->updateOnDemand("fake_cluster");
}

// Tests that a regular (non-ADS) config source is passed to the per-resource subscriptions, that
// the updates are applied, and that different config sources do not share the subscriptions.
TEST_F(XdstpOdCdsApiImplTest, RegularConfigSourceSubscriptions) {
  InSequence s;

  const auto config_source1 = TestUtility::parseYaml<envoy::config::core::v3::ConfigSource>(R"EOF(
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster1
  )EOF");
  const auto config_source2 = TestUtility::parseYaml<envoy::config::core::v3::ConfigSource>(R"EOF(
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster2
  )EOF");
  OptRef<xds::core::v3::ResourceLocator> null_locator;
  auto odcds1 =
      *XdstpOdCdsApiImpl::create(config_source1, null_locator, xds_manager_, cm_, notifier_,
                                 *store_.rootScope(), validation_visitor_, server_factory_context_);
  auto odcds2 =
      *XdstpOdCdsApiImpl::create(config_source2, null_locator, xds_manager_, cm_, notifier_,
                                 *store_.rootScope(), validation_visitor_, server_factory_context_);

  const std::string cluster_name = "fake_cluster";
  auto expect_subscription = [&](const envoy::config::core::v3::ConfigSource& expected_config) {
    EXPECT_CALL(xds_manager_, subscribeToSingletonResource(cluster_name, _, _, _, _, _, _))
        .WillOnce(Invoke(
            [this, &expected_config](
                absl::string_view,
                OptRef<const envoy::config::core::v3::ConfigSource> config_source,
                absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                Config::OpaqueResourceDecoderSharedPtr,
                const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              EXPECT_TRUE(config_source.has_value());
              EXPECT_THAT(config_source.ref(), ProtoEq(expected_config));
              odcds_callbacks_ = &callbacks;
              return std::make_unique<NiceMock<Config::MockSubscription>>();
            }));
  };

  expect_subscription(config_source1);
  odcds1->updateOnDemand(cluster_name);
  ASSERT_NE(odcds_callbacks_, nullptr);
  Config::SubscriptionCallbacks* callbacks1 = odcds_callbacks_;

  // The same resource over the same config source is not subscribed to again.
  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(_, _, _, _, _, _, _)).Times(0);
  odcds1->updateOnDemand(cluster_name);

  // The same resource over a different config source gets its own subscription.
  expect_subscription(config_source2);
  odcds2->updateOnDemand(cluster_name);

  // Updates received on the subscription are applied to the cluster manager.
  const auto cluster =
      TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
    name: {}
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF",
                                                                              cluster_name));
  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));
  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), cluster_name, {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);
  EXPECT_OK(callbacks1->onConfigUpdate(resources, {}, version));
}

// Tests that the fields of an ADS config source (e.g., initial_fetch_timeout) are honored, and that
// ADS config sources with different fields do not share the subscriptions.
TEST_F(XdstpOdCdsApiImplTest, AdsConfigSourceHonored) {
  InSequence s;

  const auto config_source1 = TestUtility::parseYaml<envoy::config::core::v3::ConfigSource>(R"EOF(
    ads: {}
    initial_fetch_timeout: 1s
  )EOF");
  const auto config_source2 = TestUtility::parseYaml<envoy::config::core::v3::ConfigSource>(R"EOF(
    ads: {}
    initial_fetch_timeout: 2s
  )EOF");
  OptRef<xds::core::v3::ResourceLocator> null_locator;
  auto odcds1 =
      *XdstpOdCdsApiImpl::create(config_source1, null_locator, xds_manager_, cm_, notifier_,
                                 *store_.rootScope(), validation_visitor_, server_factory_context_);
  auto odcds2 =
      *XdstpOdCdsApiImpl::create(config_source2, null_locator, xds_manager_, cm_, notifier_,
                                 *store_.rootScope(), validation_visitor_, server_factory_context_);

  const std::string cluster_name = "fake_cluster";
  auto expect_subscription = [&](const envoy::config::core::v3::ConfigSource& expected_config) {
    EXPECT_CALL(xds_manager_, subscribeToSingletonResource(cluster_name, _, _, _, _, _, _))
        .WillOnce(Invoke(
            [&expected_config](
                absl::string_view,
                OptRef<const envoy::config::core::v3::ConfigSource> config_source,
                absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks&,
                Config::OpaqueResourceDecoderSharedPtr,
                const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              EXPECT_TRUE(config_source.has_value());
              EXPECT_THAT(config_source.ref(), ProtoEq(expected_config));
              return std::make_unique<NiceMock<Config::MockSubscription>>();
            }));
  };

  expect_subscription(config_source1);
  odcds1->updateOnDemand(cluster_name);

  // The same resource over the same config source is not subscribed to again.
  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(_, _, _, _, _, _, _)).Times(0);
  odcds1->updateOnDemand(cluster_name);

  // The same resource over an ADS config source with different fields gets its own subscription.
  expect_subscription(config_source2);
  odcds2->updateOnDemand(cluster_name);
}
} // namespace
} // namespace Upstream
} // namespace Envoy
