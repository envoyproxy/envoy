#include <memory>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"

#include "common/common/empty_string.h"
#include "common/config/new_grpc_mux_impl.h"
#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
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
        control_plane_connected_state_(
            stats_.gauge("control_plane.connected_state", Stats::Gauge::ImportMode::NeverImport)) {}

  void setup() {
    grpc_mux_ = std::make_unique<NewGrpcMuxImpl>(
        std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
        random_, stats_, rate_limit_settings_, local_info_);
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Grpc::MockAsyncClient* async_client_;
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  std::unique_ptr<NewGrpcMuxImpl> grpc_mux_;
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl stats_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Stats::Gauge& control_plane_connected_state_;
};

class NewGrpcMuxImplTest : public NewGrpcMuxImplTestBase {
public:
  Event::SimulatedTimeSystem time_system_;
};

// TODO(fredlas) #8478 will delete this.
TEST_F(NewGrpcMuxImplTest, JustForCoverageTodoDelete) {
  setup();
  EXPECT_TRUE(grpc_mux_->isDelta());
}

// Test that we simply ignore a message for an unknown type_url, with no ill effects.
TEST_F(NewGrpcMuxImplTest, DiscoveryResponseNonexistentSub) {
  setup();

  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  grpc_mux_->addOrUpdateWatch(type_url, nullptr, {}, callbacks_, std::chrono::milliseconds(0));

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();

  {
    auto unexpected_response = std::make_unique<envoy::api::v2::DeltaDiscoveryResponse>();
    unexpected_response->set_type_url(type_url);
    unexpected_response->set_system_version_info("0");
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "0")).Times(0);
    grpc_mux_->onDiscoveryResponse(std::move(unexpected_response));
  }
  {
    auto response = std::make_unique<envoy::api::v2::DeltaDiscoveryResponse>();
    response->set_type_url(type_url);
    response->set_system_version_info("1");
    envoy::api::v2::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->mutable_resource()->PackFrom(load_assignment);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, "1"))
        .WillOnce(
            Invoke([&load_assignment](
                       const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                       const Protobuf::RepeatedPtrField<std::string>&, const std::string&) {
              EXPECT_EQ(1, added_resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              added_resources[0].resource().UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
            }));
    grpc_mux_->onDiscoveryResponse(std::move(response));
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
