#include <memory>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/empty_string.h"
#include "common/config/new_grpc_mux_impl.h"
#include "common/config/protobuf_link_hacks.h"
#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/protobuf/protobuf.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

// We test some mux specific stuff below, other unit test coverage for singleton use of
// NewGrpcMuxImpl is provided in [grpc_]subscription_impl_test.cc.
class NewGrpcMuxImplTestBase : public testing::Test {
public:
  NewGrpcMuxImplTestBase()
      : async_client_(new Grpc::MockAsyncClient()),
        control_plane_stats_(Utility::generateControlPlaneStats(stats_)),
        control_plane_connected_state_(
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)) {}

  void setup() {
    grpc_mux_ = std::make_unique<NewGrpcMuxImpl>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, rate_limit_settings_,
        local_info_);
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Grpc::MockAsyncClient* async_client_;
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  std::unique_ptr<NewGrpcMuxImpl> grpc_mux_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment>
      resource_decoder_{"cluster_name"};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::TestUtil::TestStore stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  ControlPlaneStats control_plane_stats_;
  Stats::Gauge& control_plane_connected_state_;
};

class NewGrpcMuxImplTest : public NewGrpcMuxImplTestBase {
public:
  Event::SimulatedTimeSystem time_system_;
};

// Test that we simply ignore a message for an unknown type_url, with no ill effects.
TEST_F(NewGrpcMuxImplTest, DiscoveryResponseNonexistentSub) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto watch = grpc_mux_->addWatch(type_url, {}, callbacks_, resource_decoder_);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  {
    auto unexpected_response =
        std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    unexpected_response->set_type_url(type_url);
    unexpected_response->set_system_version_info("0");
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "0")).Times(0);
    grpc_mux_->onDiscoveryResponse(std::move(unexpected_response), control_plane_stats_);
  }
  {
    auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_system_version_info("1");
    envoy::config::endpoint::v3::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->mutable_resource()->PackFrom(API_DOWNGRADE(load_assignment));
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "1"))
        .WillOnce(Invoke([&load_assignment](const std::vector<DecodedResourceRef>& added_resources,
                                            const Protobuf::RepeatedPtrField<std::string>&,
                                            const std::string&) {
          EXPECT_EQ(1, added_resources.size());
          EXPECT_TRUE(
              TestUtility::protoEqual(added_resources[0].get().resource(), load_assignment));
        }));
    grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);
  }
}

// DeltaDiscoveryResponse that comes in response to an on-demand request updates the watch with
// resource's name. The watch is initially created with an alias used in the on-demand request.
TEST_F(NewGrpcMuxImplTest, ConfigUpdateWithAliases) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().VirtualHost;
  auto watch = grpc_mux_->addWatch(type_url, {"domain1.test"}, callbacks_, resource_decoder_);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  envoy::config::route::v3::VirtualHost vhost;
  vhost.set_name("vhost_1");
  vhost.add_domains("domain1.test");
  vhost.add_domains("domain2.test");

  response->add_resources()->mutable_resource()->PackFrom(vhost);
  response->mutable_resources()->at(0).set_name("vhost_1");
  response->mutable_resources()->at(0).add_aliases("domain1.test");
  response->mutable_resources()->at(0).add_aliases("domain2.test");

  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);

  const auto& subscriptions = grpc_mux_->subscriptions();
  auto sub = subscriptions.find(type_url);

  EXPECT_TRUE(sub != subscriptions.end());
  watch->update({});
}

// DeltaDiscoveryResponse that comes in response to an on-demand request that couldn't be resolved
// will contain an empty Resource. The Resource's aliases field will be populated with the alias
// originally used in the request.
TEST_F(NewGrpcMuxImplTest, ConfigUpdateWithNotFoundResponse) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().VirtualHost;
  auto watch = grpc_mux_->addWatch(type_url, {"domain1.test"}, callbacks_, resource_decoder_);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
  response->set_type_url(type_url);
  response->set_system_version_info("1");

  response->add_resources();
  response->mutable_resources()->at(0).set_name("not-found");
  response->mutable_resources()->at(0).add_aliases("domain1.test");

  grpc_mux_->onDiscoveryResponse(std::move(response), control_plane_stats_);

  const auto& subscriptions = grpc_mux_->subscriptions();
  auto sub = subscriptions.find(type_url);

  EXPECT_TRUE(sub != subscriptions.end());
  watch->update({});
}

} // namespace
} // namespace Config
} // namespace Envoy
