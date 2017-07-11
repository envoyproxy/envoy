#pragma once

#include "common/config/grpc_subscription_impl.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Config {

typedef Grpc::MockAsyncClient<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>
    SubscriptionMockAsyncClient;
typedef GrpcSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> GrpcEdsSubscriptionImpl;

class GrpcSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  GrpcSubscriptionTestHarness()
      : method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints")),
        async_client_(new SubscriptionMockAsyncClient()), timer_(new Event::MockTimer()) {
    node_.set_id("fo0");
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    subscription_.reset(new GrpcEdsSubscriptionImpl(
        node_, std::unique_ptr<SubscriptionMockAsyncClient>(async_client_), dispatcher_,
        *method_descriptor_, stats_));
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) override {
    envoy::api::v2::DiscoveryRequest expected_request;
    expected_request.mutable_node()->CopyFrom(node_);
    for (const auto& cluster : cluster_names) {
      expected_request.add_resource_names(cluster);
    }
    if (!version.empty()) {
      expected_request.set_version_info(version);
    }
    EXPECT_CALL(async_stream_, sendMessage(ProtoEq(expected_request)));
  }

  void startSubscription(const std::vector<std::string>& cluster_names) override {
    EXPECT_CALL(*async_client_, start(_, _, _)).WillOnce(Return(&async_stream_));
    expectSendMessage(cluster_names, "");
    subscription_->start(cluster_names, callbacks_);
    // These are just there to add coverage to the null implementations of these
    // callbacks.
    Http::HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{}};
    subscription_->onReceiveInitialMetadata(std::move(response_headers));
    Http::TestHeaderMapImpl request_headers;
    subscription_->onCreateInitialMetadata(request_headers);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) override {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_version_info(version);
    Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> typed_resources;
    for (const auto& cluster : cluster_names) {
      envoy::api::v2::ClusterLoadAssignment* load_assignment = typed_resources.Add();
      load_assignment->set_cluster_name(cluster);
      response->add_resources()->PackFrom(*load_assignment);
    }
    EXPECT_CALL(callbacks_, onConfigUpdate(RepeatedProtoEq(typed_resources)))
        .WillOnce(ThrowOnRejectedConfig(accept));
    if (accept) {
      expectSendMessage(cluster_names, version);
      version_ = version;
    } else {
      expectSendMessage(cluster_names, version_);
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
    }
    subscription_->onReceiveMessage(std::move(response));
    Mock::VerifyAndClearExpectations(&async_stream_);
  }

  void updateResources(const std::vector<std::string>& cluster_names) override {
    subscription_->updateResources(cluster_names);
  }

  std::string version_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  SubscriptionMockAsyncClient* async_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  envoy::api::v2::Node node_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  Grpc::MockAsyncClientStream<envoy::api::v2::DiscoveryRequest> async_stream_;
  std::unique_ptr<GrpcEdsSubscriptionImpl> subscription_;
};

} // namespace Config
} // namespace Envoy
