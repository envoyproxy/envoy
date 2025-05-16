#include <chrono>

#include "envoy/upstream/cluster_manager.h"

#include "source/common/config/xds_resource.h"

#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/mocks/upstream/od_cds_api.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

class ODCDTest : public ClusterManagerImplTest {
public:
  void SetUp() override {
    create(defaultConfig());
    odcds_ = MockOdCdsApi::create();
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
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo"), "version1").ok());
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
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_irrelevant"), "version1")
          .ok());
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

  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo1"), "version1").ok());
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo2"), "version1").ok());
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo3"), "version1").ok());
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo4"), "version1").ok());
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

  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo"), "version1").ok());
  EXPECT_EQ(callback_call_count_, 2);
}

// Check if callback is invoked when trying to discover a cluster we already know about. It should
// not call into ODCDS in such case.
TEST_F(ODCDTest, TestCallbackWithExistingCluster) {
  auto cb = createCallback(ClusterDiscoveryStatus::Available);
  ASSERT_TRUE(
      cluster_manager_->addOrUpdateCluster(defaultStaticCluster("cluster_foo"), "version1").ok());
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

} // namespace
} // namespace Upstream
} // namespace Envoy
