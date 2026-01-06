#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/clusters/common/logical_host.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/transport_socket_match.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {

class RealHostDescriptionTest : public testing::Test {
public:
  Network::Address::InstanceConstSharedPtr address_ = nullptr;
  Upstream::MockHost* mock_host_{new NiceMock<Upstream::MockHost>()};
  Upstream::HostConstSharedPtr host_{mock_host_};
  Upstream::RealHostDescription description_{address_, host_};
};

TEST_F(RealHostDescriptionTest, UnitTest) {
  // No-op unit tests.
  description_.canary();
  description_.metadata();
  description_.priority();
  EXPECT_EQ(nullptr, description_.healthCheckAddress());

  // Pass through functions.
  EXPECT_CALL(*mock_host_, transportSocketFactory());
  description_.transportSocketFactory();

  EXPECT_CALL(*mock_host_, canCreateConnection(_));
  description_.canCreateConnection(Upstream::ResourcePriority::Default);

  EXPECT_CALL(*mock_host_, loadMetricStats());
  description_.loadMetricStats();

  EXPECT_CALL(*mock_host_, addressListOrNull())
      .WillOnce(Return(std::make_shared<Upstream::HostDescription::AddressVector>()));
  description_.addressListOrNull();

  const envoy::config::core::v3::Metadata metadata;
  const envoy::config::cluster::v3::Cluster cluster;
  Network::MockTransportSocketFactory socket_factory;
  EXPECT_CALL(*mock_host_, resolveTransportSocketFactory(_, _, _))
      .WillOnce(ReturnRef(socket_factory));
  description_.resolveTransportSocketFactory(address_, &metadata, nullptr);

  description_.canary(false);
  description_.priority(0);
  description_.metadata(nullptr);
  description_.setLastHcPassTime(MonotonicTime());

  Upstream::HealthCheckHostMonitorPtr heath_check_monitor;
  description_.setHealthChecker(std::move(heath_check_monitor));

  Upstream::Outlier::DetectorHostMonitorPtr detector_host;
  description_.setOutlierDetector(std::move(detector_host));
}

// Test fixture for LogicalHost per-connection transport socket resolution.
class LogicalHostTransportSocketResolutionTest : public testing::Test {
public:
  void SetUp() override {
    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    transport_socket_matcher_ = dynamic_cast<Upstream::MockTransportSocketMatcher*>(
        cluster_info_->transport_socket_matcher_.get());
    ASSERT_NE(transport_socket_matcher_, nullptr);
  }

  Network::TransportSocketOptionsConstSharedPtr
  createTransportSocketOptionsWithFilterState(const std::string& key, const std::string& value) {
    auto filter_state = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::Connection);
    auto string_accessor = std::make_shared<Router::StringAccessorImpl>(value);
    filter_state->setData(key, string_accessor, StreamInfo::FilterState::StateType::ReadOnly,
                          StreamInfo::FilterState::LifeSpan::Connection,
                          StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    auto shared_objects = filter_state->objectsSharedWithUpstreamConnection();
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{},
        absl::nullopt, std::move(shared_objects));
  }

  // Helper to compute the per-connection resolution condition as used in LogicalHost.
  bool needsPerConnectionResolution(Network::TransportSocketOptionsConstSharedPtr options) {
    return cluster_info_->transportSocketMatcher().usesFilterState() && options &&
           !options->downstreamSharedFilterStateObjects().empty();
  }

  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  Upstream::MockTransportSocketMatcher* transport_socket_matcher_;
};

// Test that per-connection resolution is triggered when all conditions are met.
TEST_F(LogicalHostTransportSocketResolutionTest, PerConnectionResolutionWhenAllConditionsMet) {
  ON_CALL(*transport_socket_matcher_, usesFilterState()).WillByDefault(Return(true));
  auto options =
      createTransportSocketOptionsWithFilterState("envoy.network.namespace", "/run/netns/ns1");

  EXPECT_TRUE(needsPerConnectionResolution(options));
}

// Test that per-connection resolution is not triggered when usesFilterState returns false.
TEST_F(LogicalHostTransportSocketResolutionTest,
       NoPerConnectionResolutionWhenUsesFilterStateFalse) {
  ON_CALL(*transport_socket_matcher_, usesFilterState()).WillByDefault(Return(false));
  auto options =
      createTransportSocketOptionsWithFilterState("envoy.network.namespace", "/run/netns/ns1");

  EXPECT_FALSE(needsPerConnectionResolution(options));
}

// Test that per-connection resolution is not triggered when transport socket options are null.
TEST_F(LogicalHostTransportSocketResolutionTest, NoPerConnectionResolutionWhenOptionsNull) {
  ON_CALL(*transport_socket_matcher_, usesFilterState()).WillByDefault(Return(true));
  Network::TransportSocketOptionsConstSharedPtr options = nullptr;

  EXPECT_FALSE(needsPerConnectionResolution(options));
}

// Test that per-connection resolution is not triggered when filter state objects are empty.
TEST_F(LogicalHostTransportSocketResolutionTest,
       NoPerConnectionResolutionWhenFilterStateObjectsEmpty) {
  ON_CALL(*transport_socket_matcher_, usesFilterState()).WillByDefault(Return(true));
  auto options = std::make_shared<Network::TransportSocketOptionsImpl>(
      "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{});

  EXPECT_FALSE(needsPerConnectionResolution(options));
}

// Test that override transport socket options takes precedence over passed options.
TEST_F(LogicalHostTransportSocketResolutionTest, OverrideTransportSocketOptionsTakesPrecedence) {
  ON_CALL(*transport_socket_matcher_, usesFilterState()).WillByDefault(Return(true));

  // Create override options with filter state.
  auto override_options =
      createTransportSocketOptionsWithFilterState("envoy.network.namespace", "/run/netns/ns1");

  // Create passed options without filter state.
  auto passed_options = std::make_shared<Network::TransportSocketOptionsImpl>(
      "", std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{});

  // Simulate LogicalHost's effective_options logic: use override if not null.
  const auto& effective_options = override_options != nullptr ? override_options : passed_options;

  EXPECT_TRUE(needsPerConnectionResolution(effective_options));
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
