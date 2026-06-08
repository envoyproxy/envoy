#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/clusters/common/logical_host.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/transport_socket_match.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

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
  EXPECT_EQ(nullptr, description_.orcaReportingAddress());

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

  EXPECT_CALL(*mock_host_, lbPolicyDataCount());
  description_.lbPolicyDataCount();

  EXPECT_CALL(*mock_host_, lbPolicyDataAt(0));
  description_.lbPolicyDataAt(0);

  description_.canary(false);
  description_.priority(0);
  description_.metadata(nullptr);
  description_.setLastHcPassTime(MonotonicTime());
  description_.addLbPolicyData(nullptr);

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
    filter_state->setData(key, string_accessor, StreamInfo::FilterState::LifeSpan::Connection,
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

// Test fixture for LogicalHost::createOrcaReportingConnection override. This validates that the
// override snapshots address state under the host's lock and routes through the transport socket
// matcher when caller-supplied metadata is non-null.
class LogicalHostOrcaReportingConnectionTest : public testing::Test {
protected:
  void SetUp() override {
    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    address_ = *Network::Utility::resolveUrl("tcp://10.0.0.1:1234");
    auto host_or_error = Upstream::LogicalHost::create(
        cluster_info_, /*hostname=*/"", address_, /*address_list=*/{},
        /*locality_lb_endpoint=*/envoy::config::endpoint::v3::LocalityLbEndpoints(),
        /*lb_endpoint=*/envoy::config::endpoint::v3::LbEndpoint(),
        /*override_transport_socket_options=*/nullptr);
    ASSERT_TRUE(host_or_error.ok());
    host_ = std::shared_ptr<Upstream::LogicalHost>(std::move(*host_or_error));
  }

  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  Network::Address::InstanceConstSharedPtr address_;
  std::shared_ptr<Upstream::LogicalHost> host_;
};

TEST_F(LogicalHostOrcaReportingConnectionTest, DialsHostDataAddress) {
  Event::MockDispatcher dispatcher;
  StrictMock<Network::MockClientConnection>* connection =
      new StrictMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher, createClientConnection_(address_, _, _, _)).WillOnce(Return(connection));
  EXPECT_CALL(*connection, setBufferLimits(_)).Times(AnyNumber());
  EXPECT_CALL(*connection, connectionInfoSetter()).Times(AnyNumber());
  EXPECT_CALL(*connection, streamInfo()).Times(AnyNumber());
  Upstream::Host::CreateConnectionData data =
      host_->createOrcaReportingConnection(dispatcher, nullptr, nullptr);
  EXPECT_EQ(data.host_description_->address(), address_);
}

TEST_F(LogicalHostOrcaReportingConnectionTest, MetadataRoutesThroughMatcher) {
  auto* matcher = dynamic_cast<Upstream::MockTransportSocketMatcher*>(
      cluster_info_->transport_socket_matcher_.get());
  ASSERT_NE(matcher, nullptr);
  envoy::config::core::v3::Metadata md;
  EXPECT_CALL(*matcher, resolve(&md, _, _))
      .WillOnce(Return(Upstream::TransportSocketMatcher::MatchData(*matcher->socket_factory_,
                                                                   matcher->stats_, "orca-md")));
  Event::MockDispatcher dispatcher;
  StrictMock<Network::MockClientConnection>* connection =
      new StrictMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher, createClientConnection_(address_, _, _, _)).WillOnce(Return(connection));
  EXPECT_CALL(*connection, setBufferLimits(_)).Times(AnyNumber());
  EXPECT_CALL(*connection, connectionInfoSetter()).Times(AnyNumber());
  EXPECT_CALL(*connection, streamInfo()).Times(AnyNumber());
  host_->createOrcaReportingConnection(dispatcher, nullptr, &md);
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
