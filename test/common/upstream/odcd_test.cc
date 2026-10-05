#include <chrono>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/thread.h"
#include "source/common/config/xds_resource.h"

#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/od_cds_api.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

class ODCDTest : public ClusterManagerImplTest {
public:
  void SetUp() override {
    create(defaultConfig());
    odcds_ = MockOdCdsApi::create();
    // The cluster manager consults isKnownMissing() on every discovery request. Expect that in
    // all tests (answered false by default) while keeping the mock strict for everything else;
    // individual tests override this with their own expectations.
    EXPECT_CALL(*odcds_, isKnownMissing(_)).Times(testing::AnyNumber());
    odcds_handle_ = cluster_manager_->createOdCdsApiHandle(odcds_);
  }

  void TearDown() override {
    odcds_.reset();
    odcds_handle_.reset();
    factory_.tls_.shutdownThread();
  }

  ClusterDiscoveryCallbackPtr createCallback() {
    return std::make_unique<ClusterDiscoveryCallback>(
        [this](ClusterDiscoveryStatus cluster_status) {
          UNREFERENCED_PARAMETER(cluster_status);
          ++callback_call_count_;
        });
  }

  ClusterDiscoveryCallbackPtr createCallback(ClusterDiscoveryStatus expected_cluster_status) {
    return std::make_unique<ClusterDiscoveryCallback>(
        [this, expected_cluster_status](ClusterDiscoveryStatus cluster_status) {
          EXPECT_EQ(expected_cluster_status, cluster_status);
          ++callback_call_count_;
        });
  }

  MockOdCdsApiSharedPtr odcds_;
  OdCdsApiHandlePtr odcds_handle_;
  std::chrono::milliseconds timeout_ = std::chrono::milliseconds(5000);
  unsigned callback_call_count_ = 0u;
};

// Check that we create a valid handle for valid config source and null resource locator.
TEST_F(ODCDTest, TestAllocate) {
  envoy::config::core::v3::ConfigSource config;
  OptRef<xds::core::v3::ResourceLocator> locator;
  ProtobufMessage::MockValidationVisitor mock_visitor;

  config.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
  config.mutable_api_config_source()->set_transport_api_version(envoy::config::core::v3::V3);
  config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster");

  auto handle =
      *cluster_manager_->allocateOdCdsApi(&OdCdsApiImpl::create, config, locator, mock_visitor);
  EXPECT_NE(handle, nullptr);
}

// Check that we create a valid handle for valid config source and resource locator.
TEST_F(ODCDTest, TestAllocateWithLocator) {
  envoy::config::core::v3::ConfigSource config;
  ProtobufMessage::MockValidationVisitor mock_visitor;

  config.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
  config.mutable_api_config_source()->set_transport_api_version(envoy::config::core::v3::V3);
  config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster");

  auto locator =
      Config::XdsResourceIdentifier::decodeUrl("xdstp://foo/envoy.config.cluster.v3.Cluster/bar")
          .value();
  auto handle =
      *cluster_manager_->allocateOdCdsApi(&OdCdsApiImpl::create, config, locator, mock_visitor);
  EXPECT_NE(handle, nullptr);
}

// Check if requesting for an unknown cluster calls into ODCDS instead of invoking the callback.
TEST_F(ODCDTest, TestRequest) {
  auto cb = createCallback();
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 0);
}

// Check if repeatedly requesting for an unknown cluster calls only once into ODCDS instead of
// invoking the callbacks.
TEST_F(ODCDTest, TestRequestRepeated) {
  auto cb1 = createCallback();
  auto cb2 = createCallback();
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle1 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb1), timeout_);
  auto handle2 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb2), timeout_);
  EXPECT_EQ(callback_call_count_, 0);
}

// Check if requesting an unknown cluster calls into ODCDS, even after the successful discovery of
// the cluster and its following expiration (removal). Also make sure that the callback is called on
// the successful discovery.
TEST_F(ODCDTest, TestClusterRediscovered) {
  auto cb = createCallback(ClusterDiscoveryStatus::Available);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo")).Times(2);
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo"), "version1"));
  EXPECT_EQ(callback_call_count_, 1);
  handle.reset();
  cluster_manager_->removeCluster("cluster_foo");
  cb = createCallback();
  handle = odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 1);
}

// Check if requesting an unknown cluster calls into ODCDS, even after the expired discovery of the
// cluster. Also make sure that the callback is called on the expired discovery.
TEST_F(ODCDTest, TestClusterRediscoveredAfterExpiration) {
  auto cb = createCallback(ClusterDiscoveryStatus::Timeout);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo")).Times(2);
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  cluster_manager_->notifyExpiredDiscovery("cluster_foo");
  EXPECT_EQ(callback_call_count_, 1);
  handle.reset();
  cb = createCallback();
  handle = odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 1);
}

// Check if requesting an unknown cluster calls into ODCDS, even after
// the discovery found out that the cluster is missing in the
// management server. Also make sure that the callback is called on
// the failed discovery.
TEST_F(ODCDTest, TestClusterRediscoveredAfterMissing) {
  auto cb = createCallback(ClusterDiscoveryStatus::Missing);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo")).Times(2);
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  cluster_manager_->notifyMissingCluster("cluster_foo");
  EXPECT_EQ(callback_call_count_, 1);
  handle.reset();
  cb = createCallback();
  handle = odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 1);
}

// Check that we do nothing if we get a notification about irrelevant
// missing cluster.
TEST_F(ODCDTest, TestIrrelevantNotifyMissingCluster) {
  auto cb = createCallback(ClusterDiscoveryStatus::Timeout);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  cluster_manager_->notifyMissingCluster("cluster_bar");
  EXPECT_EQ(callback_call_count_, 0);
}

// Check that the callback is not called when some other cluster is added.
TEST_F(ODCDTest, TestDiscoveryManagerIgnoresIrrelevantClusters) {
  auto cb = std::make_unique<ClusterDiscoveryCallback>([](ClusterDiscoveryStatus) {
    ADD_FAILURE() << "The callback should not be called for irrelevant clusters";
  });
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  ASSERT_OK(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_irrelevant"), "version1"));
}

// Start a couple of discoveries and drop the discovery handles in different order, make sure no
// callbacks are invoked when discoveries are done.
TEST_F(ODCDTest, TestDroppingHandles) {
  auto cb1 = std::make_unique<ClusterDiscoveryCallback>(
      [](ClusterDiscoveryStatus) { ADD_FAILURE() << "The callback 1 should not be called"; });
  auto cb2 = std::make_unique<ClusterDiscoveryCallback>(
      [](ClusterDiscoveryStatus) { ADD_FAILURE() << "The callback 2 should not be called"; });
  auto cb3 = std::make_unique<ClusterDiscoveryCallback>(
      [](ClusterDiscoveryStatus) { ADD_FAILURE() << "The callback 3 should not be called"; });
  auto cb4 = std::make_unique<ClusterDiscoveryCallback>(
      [](ClusterDiscoveryStatus) { ADD_FAILURE() << "The callback 4 should not be called"; });
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo1"));
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo2"));
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo3"));
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo4"));
  auto handle1 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo1", std::move(cb1), timeout_);
  auto handle2 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo2", std::move(cb2), timeout_);
  auto handle3 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo3", std::move(cb3), timeout_);
  auto handle4 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo4", std::move(cb4), timeout_);

  handle2.reset();
  handle3.reset();
  handle1.reset();
  handle4.reset();

  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo1"), "version1"));
  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo2"), "version1"));
  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo3"), "version1"));
  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo4"), "version1"));
}

// Checks that dropping discovery handles will result in callbacks not being invoked.
TEST_F(ODCDTest, TestHandles) {
  auto cb1 = createCallback(ClusterDiscoveryStatus::Available);
  auto cb2 = std::make_unique<ClusterDiscoveryCallback>(
      [](ClusterDiscoveryStatus) { ADD_FAILURE() << "The callback 2 should not be called"; });
  auto cb3 = std::make_unique<ClusterDiscoveryCallback>(
      [](ClusterDiscoveryStatus) { ADD_FAILURE() << "The callback 3 should not be called"; });
  auto cb4 = createCallback(ClusterDiscoveryStatus::Available);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle1 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb1), timeout_);
  auto handle2 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb2), timeout_);
  auto handle3 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb3), timeout_);
  auto handle4 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb4), timeout_);

  // handle1 and handle4 are left intact, so their respective callbacks will be invoked.
  handle2.reset();
  handle3.reset();

  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo"), "version1"));
  EXPECT_EQ(callback_call_count_, 2);
}

// Check if callback is invoked when trying to discover a cluster we already know about. It should
// not call into ODCDS in such case.
TEST_F(ODCDTest, TestCallbackWithExistingCluster) {
  auto cb = createCallback(ClusterDiscoveryStatus::Available);
  ASSERT_OK(cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo"), "version1"));
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo")).Times(0);
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 1);
}

// Checks that the cluster manager detects that a thread has requested a cluster that some other
// thread already did earlier, so it does not start another discovery process.
TEST_F(ODCDTest, TestMainThreadDiscoveryInProgressDetection) {
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto cb1 = createCallback();
  auto cb2 = createCallback();
  auto handle1 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb1), timeout_);
  auto cdm = cluster_manager_->createAndSwapClusterDiscoveryManager("another_fake_thread");
  auto handle2 =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb2), timeout_);
}

// Test that destroying an OdCdsApiHandle from a worker thread does not cause SIGABRT.
// The handle no longer accesses the subscription directly, so destruction is safe
// from any thread. The subscription itself persists in ClusterManagerImpl.
TEST_F(ODCDTest, TestDestroyHandleFromWorkerThread) {
  auto handle_to_destroy = cluster_manager_->createOdCdsApiHandle(odcds_);

  bool destruction_completed = false;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr worker_dispatcher(api->allocateDispatcher("test_worker_thread"));

  Thread::ThreadPtr worker_thread = Thread::threadFactoryForTest().createThread(
      [&handle_to_destroy, &destruction_completed, &worker_dispatcher]() {
        Thread::SkipAsserts skip;

        EXPECT_FALSE(Thread::MainThread::isMainThread());

        handle_to_destroy.reset();
        destruction_completed = true;

        worker_dispatcher->run(Event::Dispatcher::RunType::NonBlock);
      });

  worker_thread->join();
  EXPECT_TRUE(destruction_completed);
}

// Check that a request for a cluster the ODCDS source already knows to be missing is answered
// immediately from that remembered answer, without starting another discovery.
TEST_F(ODCDTest, TestKnownMissingClusterAnsweredImmediately) {
  auto cb = createCallback(ClusterDiscoveryStatus::Missing);
  EXPECT_CALL(*odcds_, isKnownMissing("cluster_foo")).WillOnce(Return(true));
  EXPECT_CALL(*odcds_, updateOnDemand(_)).Times(0);
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 1);
}

// Check that a source that doesn't know the cluster to be missing is asked as before, and the
// discovery answer still reaches the callback.
TEST_F(ODCDTest, TestNotKnownMissingClusterStartsDiscovery) {
  auto cb = createCallback(ClusterDiscoveryStatus::Missing);
  EXPECT_CALL(*odcds_, isKnownMissing("cluster_foo")).WillOnce(Return(false));
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 0);
  cluster_manager_->notifyMissingCluster("cluster_foo");
  EXPECT_EQ(callback_call_count_, 1);
}

// Check that the remembered answer is not consulted while the requested cluster is warming,
// e.g. because another config source delivered it meanwhile: the discovery proceeds and the
// cluster lifecycle callbacks answer Available once the warm-up completes.
TEST_F(ODCDTest, TestKnownMissingClusterIgnoredWhileWarming) {
  // Create a cluster that stays in the warming state: its initialization callback is captured
  // and never invoked.
  const std::string warming_cluster_yaml = R"EOF(
    name: cluster_foo
    connect_timeout: 0.250s
    type: EDS
    eds_cluster_config:
      eds_config:
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: static_cluster
  )EOF";
  const auto warming_cluster_config = parseClusterFromV3Yaml(warming_cluster_yaml);
  std::shared_ptr<MockClusterMockPrioritySet> warming_cluster =
      std::make_shared<testing::NiceMock<MockClusterMockPrioritySet>>();
  warming_cluster->info_->name_ = "cluster_foo";
  std::function<void()> cluster_init_callback;
  EXPECT_CALL(*warming_cluster, initialize(_))
      .WillOnce(testing::SaveArg<0>(&cluster_init_callback));
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _))
      .WillOnce(Return(std::make_pair(warming_cluster, nullptr)));
  ASSERT_OK(cluster_manager_->addOrUpdateCluster(warming_cluster_config, "version1"));

  // The remembered answer is never consulted; the request starts a discovery as before.
  auto cb = createCallback();
  EXPECT_CALL(*odcds_, isKnownMissing(_)).Times(0);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 0);
}

// Check that with the runtime guard disabled, the remembered answer is not consulted and a
// repeated request for a known-missing cluster calls into ODCDS again like it used to.
TEST_F(ODCDTest, TestMissingClusterCacheDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.odcds_missing_cluster_cache", "false"}});

  auto cb = createCallback();
  EXPECT_CALL(*odcds_, isKnownMissing(_)).Times(0);
  EXPECT_CALL(*odcds_, updateOnDemand("cluster_foo"));
  auto handle =
      odcds_handle_->requestOnDemandClusterDiscovery("cluster_foo", std::move(cb), timeout_);
  EXPECT_EQ(callback_call_count_, 0);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
