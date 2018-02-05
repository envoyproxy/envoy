#include "envoy/api/v2/discovery.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"

#include "common/config/grpc_mux_impl.h"
#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::IsSubstring;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Config {
namespace {

// We test some mux specific stuff below, other unit test coverage for singleton use of GrpcMuxImpl
// is provided in [grpc_]subscription_impl_test.cc.
class GrpcMuxImplTest : public testing::Test {
public:
  GrpcMuxImplTest() : async_client_(new Grpc::MockAsyncClient()), timer_(new Event::MockTimer()) {
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));

    grpc_mux_.reset(new GrpcMuxImpl(
        envoy::api::v2::core::Node(), std::unique_ptr<Grpc::MockAsyncClient>(async_client_),
        dispatcher_,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources")));
  }

  void expectSendMessage(const std::string& type_url,
                         const std::vector<std::string>& resource_names,
                         const std::string& version) {
    envoy::api::v2::DiscoveryRequest expected_request;
    expected_request.mutable_node()->CopyFrom(node_);
    for (const auto& resource : resource_names) {
      expected_request.add_resource_names(resource);
    }
    if (!version.empty()) {
      expected_request.set_version_info(version);
    }
    expected_request.set_response_nonce("");
    expected_request.set_type_url(type_url);
    EXPECT_CALL(async_stream_, sendMessage(ProtoEq(expected_request), false));
  }

  envoy::api::v2::core::Node node_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Grpc::MockAsyncClient* async_client_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<GrpcMuxImpl> grpc_mux_;
  MockGrpcMuxCallbacks callbacks_;
};

// Validate behavior when multiple type URL watches are maintained, watches are created/destroyed
// (via RAII).
TEST_F(GrpcMuxImplTest, MultipleTypeUrlStreams) {
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  auto bar_sub = grpc_mux_->subscribe("bar", {}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("bar", {}, "");
  grpc_mux_->start();
  expectSendMessage("bar", {"z"}, "");
  auto bar_z_sub = grpc_mux_->subscribe("bar", {"z"}, callbacks_);
  expectSendMessage("bar", {"zz", "z"}, "");
  auto bar_zz_sub = grpc_mux_->subscribe("bar", {"zz"}, callbacks_);
  expectSendMessage("bar", {"z"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("foo", {}, "");
}

// Validate behavior when multiple type URL watches are maintained and the stream is reset.
TEST_F(GrpcMuxImplTest, ResetStream) {
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  auto bar_sub = grpc_mux_->subscribe("bar", {}, callbacks_);
  auto baz_sub = grpc_mux_->subscribe("baz", {"z"}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("baz", {"z"}, "");
  grpc_mux_->start();

  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_)).Times(3);
  EXPECT_CALL(*timer_, enableTimer(_));
  grpc_mux_->onRemoteClose(Grpc::Status::GrpcStatus::Canceled, "");
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  expectSendMessage("bar", {}, "");
  expectSendMessage("baz", {"z"}, "");
  timer_cb_();

  expectSendMessage("baz", {}, "");
  expectSendMessage("foo", {}, "");
}

// Validate pause-resume behavior.
TEST_F(GrpcMuxImplTest, PauseResume) {
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  grpc_mux_->pause("foo");
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  grpc_mux_->start();
  expectSendMessage("foo", {"x", "y"}, "");
  grpc_mux_->resume("foo");
  grpc_mux_->pause("bar");
  expectSendMessage("foo", {"z", "x", "y"}, "");
  auto foo_z_sub = grpc_mux_->subscribe("foo", {"z"}, callbacks_);
  grpc_mux_->resume("bar");
  grpc_mux_->pause("foo");
  auto foo_zz_sub = grpc_mux_->subscribe("foo", {"zz"}, callbacks_);
  expectSendMessage("foo", {"zz", "z", "x", "y"}, "");
  grpc_mux_->resume("foo");
  grpc_mux_->pause("foo");
}

// Validate behavior when type URL mismatches occur.
TEST_F(GrpcMuxImplTest, TypeUrlMismatch) {
  InSequence s;
  auto foo_sub = grpc_mux_->subscribe("foo", {"x", "y"}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage("foo", {"x", "y"}, "");
  grpc_mux_->start();

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url("bar");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url("foo");
    response->mutable_resources()->Add()->set_type_url("bar");
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_)).WillOnce(Invoke([](const EnvoyException* e) {
      EXPECT_TRUE(
          IsSubstring("", "", "bar does not match foo type URL is DiscoveryResponse", e->what()));
    }));
    expectSendMessage("foo", {"x", "y"}, "");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  expectSendMessage("foo", {}, "");
}

// Validate behavior when watches has an unknown resource name.
TEST_F(GrpcMuxImplTest, WildcardWatch) {
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  auto foo_sub = grpc_mux_->subscribe(type_url, {}, callbacks_);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage(type_url, {}, "");
  grpc_mux_->start();

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::api::v2::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, "1"))
        .WillOnce(
            Invoke([&load_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
            }));
    expectSendMessage(type_url, {}, "1");
    grpc_mux_->onReceiveMessage(std::move(response));
  }
}

// Validate behavior when watches specify resources (potentially overlapping).
TEST_F(GrpcMuxImplTest, WatchDemux) {
  InSequence s;
  const std::string& type_url = Config::TypeUrl::get().ClusterLoadAssignment;
  MockGrpcMuxCallbacks foo_callbacks;
  auto foo_sub = grpc_mux_->subscribe(type_url, {"x", "y"}, foo_callbacks);
  MockGrpcMuxCallbacks bar_callbacks;
  auto bar_sub = grpc_mux_->subscribe(type_url, {"y", "z"}, bar_callbacks);
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  // Should dedupe the "x" resource.
  expectSendMessage(type_url, {"y", "z", "x"}, "");
  grpc_mux_->start();

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url(type_url);
    response->set_version_info("1");
    envoy::api::v2::ClusterLoadAssignment load_assignment;
    load_assignment.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "1"))
        .WillOnce(Invoke([](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                            const std::string&) { EXPECT_TRUE(resources.empty()); }));
    EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "1"))
        .WillOnce(
            Invoke([&load_assignment](const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string&) {
              EXPECT_EQ(1, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment));
            }));
    expectSendMessage(type_url, {"y", "z", "x"}, "1");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_type_url(type_url);
    response->set_version_info("2");
    envoy::api::v2::ClusterLoadAssignment load_assignment_x;
    load_assignment_x.set_cluster_name("x");
    response->add_resources()->PackFrom(load_assignment_x);
    envoy::api::v2::ClusterLoadAssignment load_assignment_y;
    load_assignment_y.set_cluster_name("y");
    response->add_resources()->PackFrom(load_assignment_y);
    envoy::api::v2::ClusterLoadAssignment load_assignment_z;
    load_assignment_z.set_cluster_name("z");
    response->add_resources()->PackFrom(load_assignment_z);
    EXPECT_CALL(bar_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke(
            [&load_assignment_y, &load_assignment_z](
                const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string&) {
              EXPECT_EQ(2, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_y));
              resources[1].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_z));
            }));
    EXPECT_CALL(foo_callbacks, onConfigUpdate(_, "2"))
        .WillOnce(Invoke(
            [&load_assignment_x, &load_assignment_y](
                const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string&) {
              EXPECT_EQ(2, resources.size());
              envoy::api::v2::ClusterLoadAssignment expected_assignment;
              resources[0].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_x));
              resources[1].UnpackTo(&expected_assignment);
              EXPECT_TRUE(TestUtility::protoEqual(expected_assignment, load_assignment_y));
            }));
    expectSendMessage(type_url, {"y", "z", "x"}, "2");
    grpc_mux_->onReceiveMessage(std::move(response));
  }

  expectSendMessage(type_url, {"x", "y"}, "2");
  expectSendMessage(type_url, {}, "2");
}

} // namespace
} // namespace Config
} // namespace Envoy
