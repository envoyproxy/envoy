#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"

#include "source/common/config/decoded_resource_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/od_cds_api_impl.h"

#include "test/mocks/config/xds_manager.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/missing_cluster_notifier.h"
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
    // Disable the shared-kAds subscription guard so these tests continue to exercise the
    // per-resource ``PerSubscriptionData`` flow, which is still the path used for genuine
    // xDS-TP resources. A dedicated fixture below covers the shared-kAds path.
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.xdstp_based_config_singleton_subscriptions", "true"},
         {"envoy.reloadable_features.odcds_singleton_shared_kads_subscription", "false"}});
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

  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate(resources, version).ok());
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

  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate(resources, version).ok());

  // Now remove the cluster.
  const std::string version2 = "v2";
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name));
  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate({}, version2).ok());
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

  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate(resources, version).ok());

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

  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate(resources2, version2).ok());
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
  EXPECT_TRUE(callbacks1->onConfigUpdate(resources, version).ok());

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
  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate(resources, version).ok());

  // Now, remove the cluster using a delta update.
  const std::string version2 = "v2";
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add(std::string(cluster_name));
  EXPECT_CALL(notifier_, notifyMissingCluster(cluster_name));
  EXPECT_TRUE(odcds_callbacks_->onConfigUpdate({}, removed_resources, version2).ok());
}

// Fixture exercising the shared-kAds subscription path (the new default behavior). This is
// activated when the on-demand filter is configured with ``ads: {}`` (the bootstrap-level ADS
// config-source) and the runtime guard
// ``envoy.reloadable_features.odcds_singleton_shared_kads_subscription`` is enabled.
class XdstpOdCdsApiImplSharedKadsTest : public testing::Test {
public:
  void SetUp() override {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.xdstp_based_config_singleton_subscriptions", "true"},
         {"envoy.reloadable_features.odcds_singleton_shared_kads_subscription", "true"}});
    // The on-demand filter constructs ``XdstpOdCdsApiImpl`` with ``ConfigSource{ ads: {} }``,
    // so ``old_ads_`` is true and the shared-kAds path is taken.
    envoy::config::core::v3::ConfigSource odcds_config;
    odcds_config.mutable_ads();
    OptRef<xds::core::v3::ResourceLocator> null_locator;
    odcds_ = *XdstpOdCdsApiImpl::create(odcds_config, null_locator, xds_manager_, cm_, notifier_,
                                        *store_.rootScope(), validation_visitor_,
                                        server_factory_context_);

    ON_CALL(xds_manager_, subscriptionFactory())
        .WillByDefault(ReturnRef(cm_.subscription_factory_));
  }

  // Captures the manager-level ``SubscriptionCallbacks``, the ``MockSubscription*``, and the
  // ``start({names})`` arg produced by the first ``subscribeToSingletonResource`` call.
  // Subsequent on-demand requests must reuse this subscription via ``requestOnDemandUpdate``
  // and not allocate a new one. ``ON_CALL`` is used for the ``start`` capture so the call is
  // observable even though it happens synchronously inside ``updateOnDemand`` before the test
  // body has any opportunity to register an ``EXPECT_CALL`` on the freshly-returned mock.
  void expectSharedSubscriptionCreation(absl::string_view first_resource_name) {
    EXPECT_CALL(xds_manager_, subscribeToSingletonResource(first_resource_name, _, _, _, _, _, _))
        .WillOnce(Invoke(
            [this](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
                   absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                   Config::OpaqueResourceDecoderSharedPtr,
                   const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              auto ret = std::make_unique<NiceMock<Config::MockSubscription>>();
              shared_subscription_ = ret.get();
              shared_callbacks_ = &callbacks;
              ON_CALL(*ret, start(_))
                  .WillByDefault(Invoke([this](const absl::flat_hash_set<std::string>& resources) {
                    observed_start_resources_ = resources;
                  }));
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
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Config::MockSubscription* shared_subscription_ = nullptr;
  Config::SubscriptionCallbacks* shared_callbacks_ = nullptr;
  absl::flat_hash_set<std::string> observed_start_resources_;
};

// The first on-demand request must construct a single shared subscription and call
// ``start({first_name})`` on it. This is the fix for the singleton manager registering one
// per-resource ``Watch`` on a shared ``WatchMap[Cluster]`` populated by a wildcard CDS watcher.
TEST_F(XdstpOdCdsApiImplSharedKadsTest, FirstRequestStartsSharedSubscription) {
  expectSharedSubscriptionCreation("cluster_a");
  odcds_->updateOnDemand("cluster_a");
  ASSERT_NE(shared_subscription_, nullptr);
  ASSERT_NE(shared_callbacks_, nullptr);
  EXPECT_THAT(observed_start_resources_, UnorderedElementsAre("cluster_a"));
}

// A second distinct on-demand request must reuse the shared subscription via
// ``requestOnDemandUpdate`` rather than allocating a new ``Subscription``. This is the
// behavior 1.37's ``OdCdsApiImpl`` exhibited and is what avoids the per-resource ``Watch``
// dispatch hazard against the wildcard CDS watcher.
TEST_F(XdstpOdCdsApiImplSharedKadsTest, SecondRequestUsesRequestOnDemandUpdate) {
  expectSharedSubscriptionCreation("cluster_a");
  odcds_->updateOnDemand("cluster_a");
  ASSERT_NE(shared_subscription_, nullptr);

  // No second ``subscribeToSingletonResource`` call must happen.
  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(_, _, _, _, _, _, _)).Times(0);
  // ``updateResourceInterest`` must carry the union of all on-demand names so the
  // underlying ``Watch`` populates ``WatchMap::watch_interest_`` and the response for
  // ``cluster_b`` is dispatched to this subscription instead of being silently dropped.
  EXPECT_CALL(*shared_subscription_,
              updateResourceInterest(absl::flat_hash_set<std::string>{"cluster_a", "cluster_b"}));
  EXPECT_CALL(*shared_subscription_,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"cluster_b"}));
  odcds_->updateOnDemand("cluster_b");
}

// Re-requesting an already-subscribed resource must short-circuit at the manager level (the
// existing ``ODCDS-manager: ... already subscribed, skipping`` debug log). The shared
// subscription must not see a redundant ``requestOnDemandUpdate``.
TEST_F(XdstpOdCdsApiImplSharedKadsTest, DuplicateRequestIsSquashed) {
  expectSharedSubscriptionCreation("cluster_a");
  odcds_->updateOnDemand("cluster_a");
  ASSERT_NE(shared_subscription_, nullptr);

  EXPECT_CALL(xds_manager_, subscribeToSingletonResource(_, _, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*shared_subscription_, updateResourceInterest(_)).Times(0);
  EXPECT_CALL(*shared_subscription_, requestOnDemandUpdate(_)).Times(0);
  odcds_->updateOnDemand("cluster_a");
}

// A ``removed_resources: [name]`` delta response on the shared subscription must invoke
// ``notifyMissingCluster(name)`` so the on_demand filter fast-fails the pending discovery
// instead of waiting for the 5s timeout.
TEST_F(XdstpOdCdsApiImplSharedKadsTest, RemovedResourceNotifiesMissingCluster) {
  expectSharedSubscriptionCreation("cluster_a");
  odcds_->updateOnDemand("cluster_a");
  ASSERT_NE(shared_callbacks_, nullptr);

  Protobuf::RepeatedPtrField<std::string> removed;
  removed.Add(std::string("cluster_a"));
  EXPECT_CALL(notifier_, notifyMissingCluster(absl::string_view{"cluster_a"}));
  EXPECT_TRUE(shared_callbacks_->onConfigUpdate({}, removed, "v1").ok());
}

// An ``added_resources: [cluster]`` delta response on the shared subscription must drive the
// cluster manager's ``addOrUpdateCluster`` exactly as the per-resource path did.
TEST_F(XdstpOdCdsApiImplSharedKadsTest, AddedResourceUpdatesClusterManager) {
  expectSharedSubscriptionCreation("cluster_a");
  odcds_->updateOnDemand("cluster_a");
  ASSERT_NE(shared_callbacks_, nullptr);

  const auto cluster = TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(R"EOF(
    name: cluster_a
    connect_timeout: 1.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
  )EOF");
  const std::string version = "v1";
  EXPECT_CALL(cm_, addOrUpdateCluster(ProtoEq(cluster), version, false));

  Config::DecodedResourceImpl decoded_resource(
      std::make_unique<envoy::config::cluster::v3::Cluster>(cluster), "cluster_a", {}, version);
  std::vector<Config::DecodedResourceRef> resources;
  resources.emplace_back(decoded_resource);
  EXPECT_TRUE(shared_callbacks_->onConfigUpdate(resources, {}, version).ok());
}

// A subscription-level failure on the shared subscription must surface as a missing-cluster
// notification for every interested resource, since the failure cannot be attributed to a
// specific name.
TEST_F(XdstpOdCdsApiImplSharedKadsTest, SubscriptionFailureNotifiesAllInterested) {
  expectSharedSubscriptionCreation("cluster_a");
  odcds_->updateOnDemand("cluster_a");
  ASSERT_NE(shared_subscription_, nullptr);

  EXPECT_CALL(*shared_subscription_,
              updateResourceInterest(absl::flat_hash_set<std::string>{"cluster_a", "cluster_b"}));
  EXPECT_CALL(*shared_subscription_,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"cluster_b"}));
  odcds_->updateOnDemand("cluster_b");

  ASSERT_NE(shared_callbacks_, nullptr);
  EXPECT_CALL(notifier_, notifyMissingCluster(absl::string_view{"cluster_a"}));
  EXPECT_CALL(notifier_, notifyMissingCluster(absl::string_view{"cluster_b"}));
  EnvoyException e("rejecting update");
  shared_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                          &e);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
