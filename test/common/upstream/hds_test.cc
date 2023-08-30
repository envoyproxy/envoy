#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/type/v3/http.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/health_discovery_service.h"
#include "source/common/upstream/transport_socket_match_impl.h"
#include "source/extensions/health_checkers/common/health_checker_base_impl.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_info_factory.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

// Friend class of HdsDelegate, making it easier to access private fields
class HdsDelegateFriend {
public:
  // Allows access to private function processMessage
  void processPrivateMessage(
      HdsDelegate& hd,
      std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
    ASSERT_TRUE(hd.processMessage(std::move(message)).ok());
  };
  HdsDelegateStats getStats(HdsDelegate& hd) { return hd.stats_; };
};

class HdsTest : public testing::Test {
protected:
  HdsTest()
      : retry_timer_(new Event::MockTimer()), server_response_timer_(new Event::MockTimer()),
        async_client_(new Grpc::MockAsyncClient()),
        api_(Api::createApiForTest(stats_store_, random_)),
        ssl_context_manager_(api_->timeSource()) {
    ON_CALL(server_context_, api()).WillByDefault(ReturnRef(*api_));
    node_.set_id("hds-node");
  }

  // Checks if the cluster counters are correct
  void checkHdsCounters(int requests, int responses, int errors, int updates) {
    auto stats = hds_delegate_friend_.getStats(*hds_delegate_);
    EXPECT_EQ(requests, stats.requests_.value());
    EXPECT_LE(responses, stats.responses_.value());
    EXPECT_EQ(errors, stats.errors_.value());
    EXPECT_EQ(updates, stats.updates_.value());
  }

  // Creates an HdsDelegate
  void createHdsDelegate() {
    InSequence s;
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillOnce(Invoke([this](Event::TimerCb timer_cb) {
          retry_timer_cb_ = timer_cb;
          return retry_timer_;
        }));
    // First call will set up the response timer for assertions, all other future calls
    // just return a new timer that we won't keep track of.
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_))
        .Times(AtLeast(1))
        .WillOnce(Invoke([this](Event::TimerCb timer_cb) {
          server_response_timer_cb_ = timer_cb;
          return server_response_timer_;
        }))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());

    hds_delegate_ = std::make_unique<HdsDelegate>(
        server_context_, *stats_store_.rootScope(), Grpc::RawAsyncClientPtr(async_client_),
        stats_store_, ssl_context_manager_, test_factory_);
  }

  void expectCreateClientConnection() {
    // Create a new mock connection for each call to createClientConnection.
    EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillRepeatedly(Invoke(
            [](Network::Address::InstanceConstSharedPtr, Network::Address::InstanceConstSharedPtr,
               Network::TransportSocketPtr&, const Network::ConnectionSocket::OptionsSharedPtr&) {
              Network::MockClientConnection* connection =
                  new NiceMock<Network::MockClientConnection>();

              // pretend our endpoint was connected to.
              connection->raiseEvent(Network::ConnectionEvent::Connected);

              // return this new, connected endpoint.
              return connection;
            }));
  }

  // Creates a HealthCheckSpecifier message that contains one endpoint and one
  // healthcheck
  envoy::service::health::v3::HealthCheckSpecifier* createSimpleMessage(bool http = true) {
    envoy::service::health::v3::HealthCheckSpecifier* msg =
        new envoy::service::health::v3::HealthCheckSpecifier;
    msg->mutable_interval()->set_seconds(1);

    auto* health_check = msg->add_cluster_health_checks();
    health_check->set_cluster_name("anna");
    health_check->add_health_checks()->mutable_timeout()->set_seconds(1);
    health_check->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
    health_check->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(2);
    health_check->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(2);
    health_check->mutable_health_checks(0)->mutable_grpc_health_check();
    if (http) {
      health_check->mutable_health_checks(0)->mutable_http_health_check()->set_codec_client_type(
          envoy::type::v3::HTTP1);
      health_check->mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck");
    }
    auto* locality_endpoints = health_check->add_locality_endpoints();
    // add locality information to this endpoint set of one endpoint.
    auto* locality = locality_endpoints->mutable_locality();
    locality->set_region("middle_earth");
    locality->set_zone("shire");
    locality->set_sub_zone("hobbiton");

    // add one endpoint to this locality grouping.
    auto* socket_address =
        locality_endpoints->add_endpoints()->mutable_address()->mutable_socket_address();
    socket_address->set_address("127.0.0.0");
    socket_address->set_port_value(1234);

    return msg;
  }

  // Creates a HealthCheckSpecifier message that contains several clusters, endpoints, localities,
  // with only one health check type.
  std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>
  createComplexSpecifier(uint32_t n_clusters, uint32_t n_localities, uint32_t n_endpoints,
                         bool disable_hc = false) {
    // Final specifier to return.
    std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier> msg =
        std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();

    // set interval.
    msg->mutable_interval()->set_seconds(1);

    for (uint32_t cluster_num = 0; cluster_num < n_clusters; cluster_num++) {
      // add a cluster with a name by iteration, with path /healthcheck
      auto* health_check = msg->add_cluster_health_checks();
      health_check->set_cluster_name(absl::StrCat("anna", cluster_num));
      health_check->add_health_checks()->mutable_timeout()->set_seconds(1);

      auto* health_check_info = health_check->mutable_health_checks(0);
      health_check_info->mutable_interval()->set_seconds(1);
      health_check_info->mutable_unhealthy_threshold()->set_value(2);
      health_check_info->mutable_healthy_threshold()->set_value(2);

      auto* health_check_http = health_check_info->mutable_http_health_check();
      health_check_http->set_codec_client_type(envoy::type::v3::HTTP1);
      health_check_http->set_path("/healthcheck");

      // add some locality groupings with iterative names for verification.
      for (uint32_t loc_num = 0; loc_num < n_localities; loc_num++) {
        auto* locality_endpoints = health_check->add_locality_endpoints();

        // set the locality information for this group.
        auto* locality = locality_endpoints->mutable_locality();
        locality->set_region(absl::StrCat("region", cluster_num));
        locality->set_zone(absl::StrCat("zone", loc_num));
        locality->set_sub_zone(absl::StrCat("subzone", loc_num));

        // add some endpoints to the locality group with iterative naming for verification.
        for (uint32_t endpoint_num = 0; endpoint_num < n_endpoints; endpoint_num++) {
          auto* endpoint = locality_endpoints->add_endpoints();

          auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
          socket_address->set_address(
              absl::StrCat("127.", cluster_num, ".", loc_num, ".", endpoint_num));
          socket_address->set_port_value(1234);
          endpoint->mutable_health_check_config()->set_disable_active_health_check(disable_hc);
        }
      }
    }

    return msg;
  }

  void
  addTransportSocketMatches(envoy::service::health::v3::ClusterHealthCheck* cluster_health_check,
                            std::string match, std::string criteria) {
    // Add transport socket matches to specified cluster and its first health check.
    const std::string match_yaml = absl::StrFormat(
        R"EOF(
transport_socket_matches:
- name: "test_socket"
  match:
    %s: "true"
  transport_socket:
    name: "envoy.transport_sockets.raw_buffer"
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
)EOF",
        match);
    cluster_health_check->MergeFrom(
        TestUtility::parseYaml<envoy::service::health::v3::ClusterHealthCheck>(match_yaml));

    // Add transport socket match criteria to our health check, for filtering matches.
    const std::string criteria_yaml = absl::StrFormat(
        R"EOF(
transport_socket_match_criteria:
  %s: "true"
)EOF",
        criteria);
    cluster_health_check->mutable_health_checks(0)->MergeFrom(
        TestUtility::parseYaml<envoy::config::core::v3::HealthCheck>(criteria_yaml));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Event::SimulatedTimeSystem time_system_;
  envoy::config::core::v3::Node node_;
  Stats::IsolatedStoreImpl stats_store_;
  MockClusterInfoFactory test_factory_;

  std::unique_ptr<Upstream::HdsDelegate> hds_delegate_;
  HdsDelegateFriend hds_delegate_friend_;

  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  Event::MockTimer* server_response_timer_;
  Event::TimerCb server_response_timer_cb_;

  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      new NiceMock<Upstream::MockClusterInfo>()};
  std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier> message;
  Grpc::MockAsyncStream async_stream_;
  Grpc::MockAsyncClient* async_client_;
  Api::ApiPtr api_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_;
  NiceMock<Random::MockRandomGenerator> random_;
};

// Test that HdsDelegate builds and sends initial message correctly
TEST_F(HdsTest, HealthCheckRequest) {
  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse request;
  request.mutable_health_check_request()->mutable_node()->set_id("hds-node");
  request.mutable_health_check_request()->mutable_capability()->add_health_check_protocols(
      envoy::service::health::v3::Capability::HTTP);
  request.mutable_health_check_request()->mutable_capability()->add_health_check_protocols(
      envoy::service::health::v3::Capability::TCP);

  EXPECT_CALL(server_context_.local_info_, node()).WillOnce(ReturnRef(node_));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(Grpc::ProtoBufferEq(request), false));
  createHdsDelegate();
}

// Test if processMessage processes endpoints from a HealthCheckSpecifier
// message correctly
TEST_F(HdsTest, TestProcessMessageEndpoints) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  // - Cluster "anna0" with 3 endpoints
  // - Cluster "anna1" with 3 endpoints
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  for (int i = 0; i < 2; i++) {
    auto* health_check = message->add_cluster_health_checks();
    health_check->set_cluster_name("anna" + std::to_string(i));
    for (int j = 0; j < 3; j++) {
      auto* locality_endpoints = health_check->add_locality_endpoints();
      locality_endpoints->mutable_locality()->set_zone(std::to_string(j));
      auto* address = locality_endpoints->add_endpoints()->mutable_address();
      address->mutable_socket_address()->set_address("127.0.0." + std::to_string(i));
      address->mutable_socket_address()->set_port_value(1234 + j);
    }
  }

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).Times(2).WillRepeatedly(Return(cluster_info_));
  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  // Check Correctness
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 3; j++) {
      auto& host =
          hds_delegate_->hdsClusters()[i]->prioritySet().hostSetsPerPriority()[0]->hosts()[j];
      EXPECT_EQ(host->address()->ip()->addressAsString(), "127.0.0." + std::to_string(i));
      EXPECT_EQ(host->address()->ip()->port(), 1234 + j);
    }
  }
}

TEST_F(HdsTest, TestHdsCluster) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  auto* health_check = message->add_cluster_health_checks();
  health_check->set_cluster_name("test_cluster");
  health_check->mutable_upstream_bind_config()->mutable_source_address()->set_address("1.1.1.1");
  auto* address = health_check->add_locality_endpoints()->add_endpoints()->mutable_address();
  address->mutable_socket_address()->set_address("127.0.0.2");
  address->mutable_socket_address()->set_port_value(1234);

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  EXPECT_EQ(hds_delegate_->hdsClusters()[0]->initializePhase(),
            Upstream::Cluster::InitializePhase::Primary);

  // HdsCluster uses health_checkers_ instead.
  EXPECT_TRUE(hds_delegate_->hdsClusters()[0]->healthChecker() == nullptr);

  // outlier detector is always null for HdsCluster.
  EXPECT_TRUE(hds_delegate_->hdsClusters()[0]->outlierDetector() == nullptr);
  const auto* hds_cluster = hds_delegate_->hdsClusters()[0].get();
  EXPECT_TRUE(hds_cluster->outlierDetector() == nullptr);
}

// Test if processMessage processes health checks from a HealthCheckSpecifier
// message correctly
TEST_F(HdsTest, TestProcessMessageHealthChecks) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  // - Cluster "minkowski0" with 2 health_checks
  // - Cluster "minkowski1" with 3 health_checks
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  for (int i = 0; i < 2; i++) {
    auto* health_check = message->add_cluster_health_checks();
    health_check->set_cluster_name("minkowski" + std::to_string(i));
    for (int j = 0; j < i + 2; j++) {
      auto hc = health_check->add_health_checks();
      hc->mutable_timeout()->set_seconds(i);
      hc->mutable_interval()->set_seconds(j);
      hc->mutable_unhealthy_threshold()->set_value(j + 1);
      hc->mutable_healthy_threshold()->set_value(j + 1);
      hc->mutable_grpc_health_check();
      hc->mutable_http_health_check()->set_codec_client_type(envoy::type::v3::HTTP1);
      hc->mutable_http_health_check()->set_path("/healthcheck");
    }
  }

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));

  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  // Check Correctness
  EXPECT_EQ(hds_delegate_->hdsClusters()[0]->healthCheckers().size(), 2);
  EXPECT_EQ(hds_delegate_->hdsClusters()[1]->healthCheckers().size(), 3);
}

// Test if processMessage exits gracefully upon receiving a malformed message
TEST_F(HdsTest, TestProcessMessageMissingFields) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());
  // remove healthy threshold field to create an error
  message->mutable_cluster_health_checks(0)->mutable_health_checks(0)->clear_healthy_threshold();

  // call onReceiveMessage function for testing. Should increment stat_ errors upon
  // getting a bad message
  hds_delegate_->onReceiveMessage(std::move(message));

  // Ensure that we never enabled the response timer that would start health checks,
  // since this config was invalid.
  EXPECT_FALSE(server_response_timer_->enabled_);

  // ensure that no partial information was stored in hds_clusters_
  EXPECT_TRUE(hds_delegate_->hdsClusters().empty());

  // Check Correctness by verifying one request and one error has been generated in stat_
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).errors_.value(), 1);
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).requests_.value(), 1);
}

// Test if processMessage exits gracefully upon receiving a malformed message
// There was a previous valid config, so we go back to that.
TEST_F(HdsTest, TestProcessMessageMissingFieldsWithFallback) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillRepeatedly(Return(connection));
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(2);
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  EXPECT_CALL(*connection, setBufferLimits(_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  connection->raiseEvent(Network::ConnectionEvent::Connected);

  // Create a invalid message
  message.reset(createSimpleMessage());

  // set this address to be distinguishable from the previous message in sendResponse()
  message->mutable_cluster_health_checks(0)
      ->mutable_locality_endpoints(0)
      ->mutable_endpoints(0)
      ->mutable_address()
      ->mutable_socket_address()
      ->set_address("9.9.9.9");

  // remove healthy threshold field to create an error
  message->mutable_cluster_health_checks(0)->mutable_health_checks(0)->clear_healthy_threshold();

  // Pass invalid message through. Should increment stat_ errors upon
  // getting a bad message.
  hds_delegate_->onReceiveMessage(std::move(message));

  // Ensure that the timer is enabled since there was a previous valid specifier.
  EXPECT_TRUE(server_response_timer_->enabled_);

  // read the response and check that it is pinging the old
  // address 127.0.0.0 instead of the new 9.9.9.9
  auto response = hds_delegate_->sendResponse();
  EXPECT_EQ(response.endpoint_health_response()
                .endpoints_health(0)
                .endpoint()
                .address()
                .socket_address()
                .address(),
            "127.0.0.0");

  // Check Correctness by verifying one request and one error has been generated in stat_
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).errors_.value(), 1);
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).requests_.value(), 2);
}

// Test if processMessage exits gracefully if the update fails
TEST_F(HdsTest, TestProcessMessageInvalidFieldsWithFallback) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillRepeatedly(Return(connection));
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  EXPECT_CALL(*connection, setBufferLimits(_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  connection->raiseEvent(Network::ConnectionEvent::Connected);

  // Create a invalid message: grpc health checks require an H2 cluster
  message.reset(createSimpleMessage(false));

  // Pass invalid message through. Should increment stat_ errors upon
  // getting a bad message.
  hds_delegate_->onReceiveMessage(std::move(message));

  // Check Correctness by verifying one request and one error has been generated in stat_
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).errors_.value(), 1);
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).requests_.value(), 2);
}

// Test if sendResponse() retains the structure of all endpoints ingested in the specifier
// from onReceiveMessage(). This verifies that all endpoints are grouped by the correct
// cluster and the correct locality.
TEST_F(HdsTest, TestSendResponseMultipleEndpoints) {
  // number of clusters, localities by cluster, and endpoints by locality
  // to build and verify off of.
  const uint32_t NumClusters = 2;
  const uint32_t NumLocalities = 2;
  const uint32_t NumEndpoints = 2;

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = createComplexSpecifier(NumClusters, NumLocalities, NumEndpoints);

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(2);
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));

  // Carry over cluster name on a call to createClusterInfo,
  // in the same way that the prod factory does.
  EXPECT_CALL(test_factory_, createClusterInfo(_))
      .WillRepeatedly(Invoke([](const ClusterInfoFactory::CreateClusterInfoParams& params) {
        std::shared_ptr<Upstream::MockClusterInfo> cluster_info{
            new NiceMock<Upstream::MockClusterInfo>()};
        // copy name for use in sendResponse() in HdsCluster

        cluster_info->name_ = params.cluster_.name();
        return cluster_info;
      }));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_))
      .Times(NumClusters * NumLocalities * NumEndpoints);

  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));

  // read response and verify fields
  const auto response = hds_delegate_->sendResponse().endpoint_health_response();

  ASSERT_EQ(response.cluster_endpoints_health_size(), NumClusters);

  for (uint32_t i = 0; i < NumClusters; i++) {
    const auto& cluster = response.cluster_endpoints_health(i);

    // Expect the correct cluster name by index
    EXPECT_EQ(cluster.cluster_name(), absl::StrCat("anna", i));

    // Every cluster should have two locality groupings
    ASSERT_EQ(cluster.locality_endpoints_health_size(), NumLocalities);

    for (uint32_t j = 0; j < NumLocalities; j++) {
      // Every locality should have a number based on its index
      const auto& loc_group = cluster.locality_endpoints_health(j);
      EXPECT_EQ(loc_group.locality().region(), absl::StrCat("region", i));
      EXPECT_EQ(loc_group.locality().zone(), absl::StrCat("zone", j));
      EXPECT_EQ(loc_group.locality().sub_zone(), absl::StrCat("subzone", j));

      // Every locality should have two endpoints.
      ASSERT_EQ(loc_group.endpoints_health_size(), NumEndpoints);

      for (uint32_t k = 0; k < NumEndpoints; k++) {

        // every endpoint's address is based on all 3 index values.
        const auto& endpoint_health = loc_group.endpoints_health(k);
        EXPECT_EQ(endpoint_health.endpoint().address().socket_address().address(),
                  absl::StrCat("127.", i, ".", j, ".", k));
        EXPECT_EQ(endpoint_health.health_status(), envoy::config::core::v3::UNHEALTHY);
      }
    }
  }
  EXPECT_EQ(response.endpoints_health_size(), NumClusters * NumLocalities * NumEndpoints);
}

// Tests OnReceiveMessage given a minimal HealthCheckSpecifier message
TEST_F(HdsTest, TestMinimalOnReceiveMessage) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  // Process message
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  hds_delegate_->onReceiveMessage(std::move(message));
}

// Test that a transport_socket_matches and transport_socket_match_criteria filter as expected to
// build the correct TransportSocketFactory based on these fields.
TEST_F(HdsTest, TestSocketContext) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message with transport sockets.
  message.reset(createSimpleMessage());
  addTransportSocketMatches(message->mutable_cluster_health_checks(0), "test_match", "test_match");

  Network::MockClientConnection* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillRepeatedly(Return(connection));

  // Pull out socket_matcher object normally internal to createClusterInfo, to test that a matcher
  // would match the expected socket.
  std::unique_ptr<TransportSocketMatcherImpl> socket_matcher;
  EXPECT_CALL(test_factory_, createClusterInfo(_))
      .WillRepeatedly(Invoke([&](const ClusterInfoFactory::CreateClusterInfoParams& params) {
        // Build scope, factory_context as does ProdClusterInfoFactory.
        Envoy::Stats::ScopeSharedPtr scope =
            params.stats_.createScope(fmt::format("cluster.{}.", params.cluster_.name()));
        Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
            params.server_context_, params.ssl_context_manager_, *scope,
            params.server_context_.clusterManager(),
            params.server_context_.messageValidationVisitor());

        // Create a mock socket_factory for the scope of this unit test.
        std::unique_ptr<Envoy::Network::UpstreamTransportSocketFactory> socket_factory =
            std::make_unique<Network::MockTransportSocketFactory>();

        // set socket_matcher object in test scope.
        socket_matcher = std::make_unique<Envoy::Upstream::TransportSocketMatcherImpl>(
            params.cluster_.transport_socket_matches(), factory_context, socket_factory, *scope);

        // But still use the fake cluster_info_.
        return cluster_info_;
      }));

  EXPECT_CALL(*connection, setBufferLimits(_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_));

  // Process message.
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  hds_delegate_->onReceiveMessage(std::move(message));

  // pretend our endpoint was connected to.
  connection->raiseEvent(Network::ConnectionEvent::Connected);

  // Get our health checker to match against.
  const auto clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(clusters.size(), 1);
  const auto hcs = clusters[0]->healthCheckers();
  ASSERT_EQ(hcs.size(), 1);

  // Check that our match hits.
  HealthCheckerImplBase* health_checker_base = dynamic_cast<HealthCheckerImplBase*>(hcs[0].get());
  const auto match =
      socket_matcher->resolve(health_checker_base->transportSocketMatchMetadata().get());
  EXPECT_EQ(match.name_, "test_socket");
}

// Tests OnReceiveMessage given a HealthCheckSpecifier message without interval field
TEST_F(HdsTest, TestDefaultIntervalOnReceiveMessage) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  // notice that interval field is intentionally left undefined

  // Process message
  EXPECT_CALL(*server_response_timer_, enableTimer(std::chrono::milliseconds(1000), _))
      .Times(AtLeast(1));
  hds_delegate_->onReceiveMessage(std::move(message));
}

// Tests that SendResponse responds to the server in a timely fashion
// given a minimal HealthCheckSpecifier message
TEST_F(HdsTest, TestMinimalSendResponse) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  // Process message and send 2 responses
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _)).Times(2);
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();
  server_response_timer_cb_();
}

TEST_F(HdsTest, TestStreamConnectionFailure) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(&async_stream_));

  EXPECT_CALL(random_, random()).WillOnce(Return(1000005)).WillRepeatedly(Return(654321));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(5), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(321), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(2321), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(6321), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(14321), _));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));

  // Test connection failure and retry
  createHdsDelegate();
  retry_timer_cb_();
  retry_timer_cb_();
  retry_timer_cb_();
  retry_timer_cb_();
  retry_timer_cb_();
}

// TODO(lilika): Add unit tests for HdsDelegate::sendResponse() with healthy and
// unhealthy endpoints.

// Tests that SendResponse responds to the server correctly given
// a HealthCheckSpecifier message that contains a single endpoint
// which times out
TEST_F(HdsTest, TestSendResponseOneEndpointTimeout) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillRepeatedly(Return(connection_));
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(2);
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  EXPECT_CALL(*connection_, setBufferLimits(_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Send Response
  auto msg = hds_delegate_->sendResponse();

  // Correctness
  EXPECT_EQ(msg.endpoint_health_response().endpoints_health(0).health_status(),
            envoy::config::core::v3::UNHEALTHY);
  EXPECT_EQ(msg.endpoint_health_response()
                .endpoints_health(0)
                .endpoint()
                .address()
                .socket_address()
                .address(),
            "127.0.0.0");
  EXPECT_EQ(msg.endpoint_health_response()
                .endpoints_health(0)
                .endpoint()
                .address()
                .socket_address()
                .port_value(),
            1234);
}

// Check to see if two of the same specifier does not get parsed twice in a row.
TEST_F(HdsTest, TestSameSpecifier) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();

  // Try to change the specifier, but it is the same.
  message.reset(createSimpleMessage());
  hds_delegate_->onReceiveMessage(std::move(message));

  // Check to see that HDS got two requests, but only used the specifier one time.
  checkHdsCounters(2, 0, 0, 1);

  // Try to change the specifier, but use a new specifier this time.
  message = createComplexSpecifier(1, 1, 2);
  hds_delegate_->onReceiveMessage(std::move(message));

  // Check that both requests and updates increased, meaning we did an update.
  checkHdsCounters(3, 0, 0, 2);
}

// Test to see that if a cluster is added or removed, the ones that did not change are reused.
TEST_F(HdsTest, TestClusterChange) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = createComplexSpecifier(2, 1, 1);

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();

  // Get cluster shared pointers to make sure they are the same memory addresses, that we reused
  // them.
  auto original_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(original_clusters.size(), 2);

  // Add a third cluster to the specifier. The first two should reuse pointers.
  message = createComplexSpecifier(3, 1, 1);
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get the new clusters list from HDS.
  auto new_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(new_clusters.size(), 3);

  // Make sure our first two clusters are at the same address in memory as before.
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(new_clusters[i], original_clusters[i]);
  }

  message = createComplexSpecifier(3, 1, 1);

  // Remove the first element, change the order of the last two elements.
  message->mutable_cluster_health_checks()->SwapElements(0, 2);
  message->mutable_cluster_health_checks()->RemoveLast();
  // Sanity check.
  ASSERT_EQ(message->cluster_health_checks_size(), 2);

  // Send this new specifier.
  hds_delegate_->onReceiveMessage(std::move(message));

  // Check to see that even if we changed the order, we get the expected pointers.
  auto final_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(final_clusters.size(), 2);

  // Compare first cluster in the new list is the same as the last in the previous list,
  // and that the second cluster in the new list is the same as the second in the previous.
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(final_clusters[i], new_clusters[2 - i]);
  }

  // Check to see that HDS got three requests, and updated three times with it.
  checkHdsCounters(3, 0, 0, 3);
}

// Edit one of two cluster's endpoints by adding and removing.
TEST_F(HdsTest, TestUpdateEndpoints) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message, and later add/remove endpoints from the second cluster.
  message.reset(createSimpleMessage());
  message->MergeFrom(*createComplexSpecifier(1, 1, 2));

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();

  // Save list of hosts/endpoints for comparison later.
  auto original_hosts = hds_delegate_->hdsClusters()[1]->hosts();
  ASSERT_EQ(original_hosts.size(), 2);

  // Add 3 endpoints to the specifier's second cluster. The first in the list should reuse pointers.
  message.reset(createSimpleMessage());
  message->MergeFrom(*createComplexSpecifier(1, 1, 5));
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get the new clusters list from HDS.
  auto new_hosts = hds_delegate_->hdsClusters()[1]->hosts();
  ASSERT_EQ(new_hosts.size(), 5);

  // Make sure our first two endpoints are at the same address in memory as before.
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(original_hosts[i], new_hosts[i]);
  }
  EXPECT_TRUE(original_hosts[0] != new_hosts[2]);

  // This time, have 4 endpoints, 2 each under 2 localities.
  // The first locality will be reused, so its 2 endpoints will be as well.
  // The second locality is new so we should be getting 2 new endpoints.
  // Since the first locality had 5 but now has 2, we are removing 3.
  // 2 ADDED, 3 REMOVED, 2 REUSED.
  message.reset(createSimpleMessage());
  message->MergeFrom(*createComplexSpecifier(1, 2, 2));
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get this new list of hosts.
  auto final_hosts = hds_delegate_->hdsClusters()[1]->hosts();
  ASSERT_EQ(final_hosts.size(), 4);

  // Ensure the first two elements in the new list are reused.
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(new_hosts[i], final_hosts[i]);
  }

  // Ensure the first last two elements in the new list are different then the previous list.
  for (int i = 2; i < 4; i++) {
    EXPECT_TRUE(new_hosts[i] != final_hosts[i]);
  }

  // Check to see that HDS got three requests, and updated three times with it.
  checkHdsCounters(3, 0, 0, 3);
}

// Skip the endpoints with disabled active health check during message processing.
TEST_F(HdsTest, TestUpdateEndpointsWithActiveHCflag) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message, and later add/remove endpoints from the second cluster.
  message.reset(createSimpleMessage());
  message->MergeFrom(*createComplexSpecifier(1, 1, 2));

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();

  // Save list of hosts/endpoints for comparison later.
  auto original_hosts = hds_delegate_->hdsClusters()[1]->hosts();
  ASSERT_EQ(original_hosts.size(), 2);

  // Ignoring the endpoints with disabled active health check.
  message.reset(createSimpleMessage());
  message->MergeFrom(*createComplexSpecifier(1, 1, 2, true));
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get the new clusters list from HDS.
  auto new_hosts = hds_delegate_->hdsClusters()[1]->hosts();
  ASSERT_EQ(new_hosts.size(), 0);
}

// Test adding, reusing, and removing health checks.
TEST_F(HdsTest, TestUpdateHealthCheckers) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message with two different health checkers.
  message.reset(createSimpleMessage());
  auto new_hc = message->mutable_cluster_health_checks(0)->add_health_checks();
  new_hc->MergeFrom(message->mutable_cluster_health_checks(0)->health_checks(0));
  new_hc->mutable_http_health_check()->set_path("/different_path");

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();

  // Save list of health checkers for use later.
  auto original_hcs = hds_delegate_->hdsClusters()[0]->healthCheckers();
  ASSERT_EQ(original_hcs.size(), 2);

  // Create a new specifier, but make the second health checker different and add a third.
  // Then reverse the order so the first one is at the end, testing the hashing works as expected.
  message.reset(createSimpleMessage());
  auto new_hc0 = message->mutable_cluster_health_checks(0)->add_health_checks();
  new_hc0->MergeFrom(message->mutable_cluster_health_checks(0)->health_checks(0));
  new_hc0->mutable_http_health_check()->set_path("/path0");
  auto new_hc1 = message->mutable_cluster_health_checks(0)->add_health_checks();
  new_hc1->MergeFrom(message->mutable_cluster_health_checks(0)->health_checks(0));
  new_hc1->mutable_http_health_check()->set_path("/path1");
  message->mutable_cluster_health_checks(0)->mutable_health_checks()->SwapElements(0, 2);
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get the new health check list from HDS.
  auto new_hcs = hds_delegate_->hdsClusters()[0]->healthCheckers();
  ASSERT_EQ(new_hcs.size(), 3);

  // Make sure our first hc from the original list is the same as the third in the new list.
  EXPECT_EQ(original_hcs[0], new_hcs[2]);
  EXPECT_TRUE(original_hcs[1] != new_hcs[1]);

  // Check to see that HDS got two requests, and updated two times with it.
  checkHdsCounters(2, 0, 0, 2);
}

// Test to see that if clusters with an empty name get used, there are two clusters.
// Also test to see that if two clusters with the same non-empty name are used, only have
// One cluster.
TEST_F(HdsTest, TestClusterSameName) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = createComplexSpecifier(2, 1, 1);
  // Set both clusters to have an empty name.
  message->mutable_cluster_health_checks(0)->set_cluster_name("");
  message->mutable_cluster_health_checks(1)->set_cluster_name("");

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();

  // Get the clusters from HDS
  auto original_clusters = hds_delegate_->hdsClusters();

  // Make sure that even though they have the same name, since they are empty there are two and they
  // do not point to the same thing.
  ASSERT_EQ(original_clusters.size(), 2);
  ASSERT_TRUE(original_clusters[0] != original_clusters[1]);

  // Create message with 3 clusters this time so we force an update.
  message = createComplexSpecifier(3, 1, 1);
  // Set both clusters to have empty names empty name.
  message->mutable_cluster_health_checks(0)->set_cluster_name("");
  message->mutable_cluster_health_checks(1)->set_cluster_name("");

  // Test that we still get requested number of clusters, even with repeated names on update since
  // they are empty.
  hds_delegate_->onReceiveMessage(std::move(message));
  auto new_clusters = hds_delegate_->hdsClusters();

  // Check that since the names are empty, we do not reuse and just reconstruct.
  ASSERT_EQ(new_clusters.size(), 3);
  ASSERT_TRUE(original_clusters[0] != new_clusters[0]);
  ASSERT_TRUE(original_clusters[1] != new_clusters[1]);

  // Create a new message.
  message = createComplexSpecifier(2, 1, 1);
  // Set both clusters to have the same, non-empty name.
  message->mutable_cluster_health_checks(0)->set_cluster_name("anna");
  message->mutable_cluster_health_checks(1)->set_cluster_name("anna");

  hds_delegate_->onReceiveMessage(std::move(message));

  // Check that since they both have the same name, only one of them gets used.
  auto final_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(final_clusters.size(), 1);

  // Check to see that HDS got three requests, and updated three times with it.
  checkHdsCounters(3, 0, 0, 3);
}

// Test that a transport_socket_matches and transport_socket_match_criteria filter fail when not
// matching, and then after an update the same cluster is used but now matches.
TEST_F(HdsTest, TestUpdateSocketContext) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create a new active connection on request, setting its status to connected
  // to mock a found endpoint.
  expectCreateClientConnection();

  // Pull out socket_matcher object normally internal to createClusterInfo, to test that a matcher
  // would match the expected socket.
  std::vector<std::unique_ptr<TransportSocketMatcherImpl>> socket_matchers;
  EXPECT_CALL(test_factory_, createClusterInfo(_))
      .WillRepeatedly(Invoke([&](const ClusterInfoFactory::CreateClusterInfoParams& params) {
        // Build scope, factory_context as does ProdClusterInfoFactory.
        Envoy::Stats::ScopeSharedPtr scope =
            params.stats_.createScope(fmt::format("cluster.{}.", params.cluster_.name()));
        Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
            params.server_context_, params.ssl_context_manager_, *scope,
            params.server_context_.clusterManager(),
            params.server_context_.messageValidationVisitor());

        // Create a mock socket_factory for the scope of this unit test.
        std::unique_ptr<Envoy::Network::UpstreamTransportSocketFactory> socket_factory =
            std::make_unique<Network::MockTransportSocketFactory>();

        // set socket_matcher object in test scope.
        socket_matchers.push_back(std::make_unique<Envoy::Upstream::TransportSocketMatcherImpl>(
            params.cluster_.transport_socket_matches(), factory_context, socket_factory, *scope));

        // But still use the fake cluster_info_.
        return cluster_info_;
      }));
  EXPECT_CALL(server_context_.dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));

  // Create Message, with a non-valid match and process.
  message.reset(createSimpleMessage());
  addTransportSocketMatches(message->mutable_cluster_health_checks(0), "bad_match", "test_match");
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get our health checker to match against.
  const auto first_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(first_clusters.size(), 1);
  const auto first_hcs = first_clusters[0]->healthCheckers();
  ASSERT_EQ(first_hcs.size(), 1);

  // Check that our fails so it uses default.
  HealthCheckerImplBase* first_health_checker_base =
      dynamic_cast<HealthCheckerImplBase*>(first_hcs[0].get());
  const auto first_match =
      socket_matchers[0]->resolve(first_health_checker_base->transportSocketMatchMetadata().get());
  EXPECT_EQ(first_match.name_, "default");

  // Create a new Message, this time with a good match.
  message.reset(createSimpleMessage());
  addTransportSocketMatches(message->mutable_cluster_health_checks(0), "test_match", "test_match");
  hds_delegate_->onReceiveMessage(std::move(message));

  // Get our new health checker to match against.
  const auto second_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(second_clusters.size(), 1);
  // Check that this new pointer is actually the same pointer to the first cluster.
  ASSERT_EQ(second_clusters[0], first_clusters[0]);
  const auto second_hcs = second_clusters[0]->healthCheckers();
  ASSERT_EQ(second_hcs.size(), 1);

  // Check that since we made no change to our health checkers, the pointer was reused.
  EXPECT_EQ(first_hcs[0], second_hcs[0]);

  // Check that our match hits.
  HealthCheckerImplBase* second_health_checker_base =
      dynamic_cast<HealthCheckerImplBase*>(second_hcs[0].get());
  ASSERT_EQ(socket_matchers.size(), 2);
  const auto second_match =
      socket_matchers[1]->resolve(second_health_checker_base->transportSocketMatchMetadata().get());
  EXPECT_EQ(second_match.name_, "test_socket");

  // Create a new Message, this we leave the transport socket the same but change the health check's
  // filter. This means that the health checker changes but the transport_socket_matches in the
  // ClusterHealthCheck does not.
  message.reset(createSimpleMessage());
  addTransportSocketMatches(message->mutable_cluster_health_checks(0), "test_match",
                            "something_new");

  hds_delegate_->onReceiveMessage(std::move(message));
  // Get our new health checker to match against.
  const auto third_clusters = hds_delegate_->hdsClusters();
  ASSERT_EQ(third_clusters.size(), 1);
  // Check that this new pointer is actually the same pointer to the first cluster.
  ASSERT_EQ(third_clusters[0], first_clusters[0]);
  const auto third_hcs = third_clusters[0]->healthCheckers();
  ASSERT_EQ(third_hcs.size(), 1);

  // Check that since we made a change to our HC, it is a new pointer.
  EXPECT_TRUE(first_hcs[0] != third_hcs[0]);

  HealthCheckerImplBase* third_health_checker_base =
      dynamic_cast<HealthCheckerImplBase*>(third_hcs[0].get());

  // Check that our socket matchers is still a size 2. This is because createClusterInfo(_) is never
  // called again since there was no update to transportSocketMatches.
  ASSERT_EQ(socket_matchers.size(), 2);
  const auto third_match =
      socket_matchers[1]->resolve(third_health_checker_base->transportSocketMatchMetadata().get());
  // Since this again does not match, it uses default.
  EXPECT_EQ(third_match.name_, "default");
}

TEST_F(HdsTest, TestCustomHealthCheckPortWhenCreate) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  // - Cluster "anna" with 3 lb_endpoints
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  auto* health_check = message->add_cluster_health_checks();
  health_check->set_cluster_name("anna");
  for (int i = 0; i < 3; i++) {
    auto* locality_endpoints = health_check->add_locality_endpoints();
    locality_endpoints->mutable_locality()->set_zone(std::to_string(i));
    auto* endpoint = locality_endpoints->add_endpoints();
    endpoint->mutable_health_check_config()->set_port_value(4321 + i);
    auto* address = endpoint->mutable_address();
    address->mutable_socket_address()->set_address("127.0.0.1");
    address->mutable_socket_address()->set_port_value(1234 + i);
  }

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  // Check Correctness
  for (int i = 0; i < 3; i++) {
    auto& host =
        hds_delegate_->hdsClusters()[0]->prioritySet().hostSetsPerPriority()[0]->hosts()[i];
    EXPECT_EQ(host->address()->ip()->port(), 1234 + i);
    EXPECT_EQ(host->healthCheckAddress()->ip()->port(), 4321 + i);
  }
}

TEST_F(HdsTest, TestCustomHealthCheckPortWhenUpdate) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  // - Cluster "anna" with 1 lb_endpoints with 3 endpoints
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  auto* health_check = message->add_cluster_health_checks();
  health_check->set_cluster_name("anna");
  auto* lb_endpoint = health_check->add_locality_endpoints();
  for (int i = 0; i < 3; i++) {
    auto* endpoint = lb_endpoint->add_endpoints();
    auto* address = endpoint->mutable_address();
    address->mutable_socket_address()->set_address("127.0.0.1");
    address->mutable_socket_address()->set_port_value(1234 + i);
  }

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  for (int i = 0; i < 3; i++) {
    auto& host =
        hds_delegate_->hdsClusters()[0]->prioritySet().hostSetsPerPriority()[0]->hosts()[i];
    EXPECT_EQ(host->address()->ip()->port(), 1234 + i);
    EXPECT_EQ(host->healthCheckAddress()->ip()->port(), 1234 + i);
  }

  // Set custom health config port
  for (int i = 0; i < 3; i++) {
    auto* endpoint =
        message->mutable_cluster_health_checks(0)->mutable_locality_endpoints(0)->mutable_endpoints(
            i);
    endpoint->mutable_health_check_config()->set_port_value(4321 + i);
  }

  // Process updating message
  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  // Check Correctness
  for (int i = 0; i < 3; i++) {
    auto& host =
        hds_delegate_->hdsClusters()[0]->prioritySet().hostSetsPerPriority()[0]->hosts()[i];
    EXPECT_EQ(host->address()->ip()->port(), 1234 + i);
    EXPECT_EQ(host->healthCheckAddress()->ip()->port(), 4321 + i);
  }
}

} // namespace Upstream
} // namespace Envoy
