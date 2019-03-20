#pragma once

#include "common/config/delta_subscription_impl.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Mock;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

typedef DeltaSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> DeltaEdsSubscriptionImpl;

class DeltaSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  DeltaSubscriptionTestHarness() : DeltaSubscriptionTestHarness(std::chrono::milliseconds(0)) {}
  DeltaSubscriptionTestHarness(std::chrono::milliseconds init_fetch_timeout)
      : method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints")),
        async_client_(new Grpc::MockAsyncClient()) {
    node_.set_id("fo0");
    EXPECT_CALL(local_info_, node()).WillRepeatedly(testing::ReturnRef(node_));
    EXPECT_CALL(dispatcher_, createTimer_(_));
    subscription_ = std::make_unique<DeltaEdsSubscriptionImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *method_descriptor_, random_, stats_store_, rate_limit_settings_, stats_,
        init_fetch_timeout);
  }

  void startSubscription(const std::vector<std::string>& cluster_names) override {
    EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
    last_cluster_names_ = cluster_names;
    expectSendMessage({}, "");
    expectSendMessage(last_cluster_names_, "");
    subscription_->start(cluster_names, callbacks_);
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) override {
    UNREFERENCED_PARAMETER(version);
    expectSendMessage(cluster_names, {}, Grpc::Status::GrpcStatus::Ok, "");
  }

  void expectSendMessage(const std::vector<std::string>& subscribe,
                         const std::vector<std::string>& unsubscribe,
                         const Protobuf::int32 error_code, const std::string& error_message) {
    envoy::api::v2::DeltaDiscoveryRequest expected_request;
    expected_request.mutable_node()->CopyFrom(node_);
    for (const auto& resource : subscribe) {
      expected_request.add_resource_names_subscribe(resource);
    }
    for (auto resource = unsubscribe.rbegin(); resource != unsubscribe.rend(); ++resource) {
      expected_request.add_resource_names_unsubscribe(*resource);
    }
    expected_request.set_response_nonce(last_response_nonce_);
    expected_request.set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);

    if (error_code != Grpc::Status::GrpcStatus::Ok) {
      ::google::rpc::Status* error_detail = expected_request.mutable_error_detail();
      error_detail->set_code(error_code);
      error_detail->set_message(error_message);
    }
    EXPECT_CALL(async_stream_, sendMessage(ProtoEq(expected_request), false));
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept) override {
    std::unique_ptr<envoy::api::v2::DeltaDiscoveryResponse> response(
        new envoy::api::v2::DeltaDiscoveryResponse());

    last_response_nonce_ = std::to_string(HashUtil::xxHash64(version));
    response->set_nonce(last_response_nonce_);
    response->set_system_version_info(version);

    Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> typed_resources;
    for (const auto& cluster : cluster_names) {
      if (std::find(last_cluster_names_.begin(), last_cluster_names_.end(), cluster) !=
          last_cluster_names_.end()) {
        envoy::api::v2::ClusterLoadAssignment* load_assignment = typed_resources.Add();
        load_assignment->set_cluster_name(cluster);
        auto* resource = response->add_resources();
        resource->set_name(cluster);
        resource->set_version(version);
        resource->mutable_resource()->PackFrom(*load_assignment);
      }
    }
    Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, version)).WillOnce(ThrowOnRejectedConfig(accept));
    if (accept) {
      expectSendMessage({}, version);
    } else {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
      expectSendMessage({}, {}, Grpc::Status::GrpcStatus::Internal, "bad config");
    }
    subscription_->onReceiveMessage(std::move(response));
    Mock::VerifyAndClearExpectations(&async_stream_);
  }

  void updateResources(const std::vector<std::string>& cluster_names) override {
    std::vector<std::string> cluster_superset = cluster_names;
    cluster_superset.insert(cluster_superset.end(), last_cluster_names_.begin(),
                            last_cluster_names_.end());
    expectSendMessage(cluster_names, last_cluster_names_, Grpc::Status::GrpcStatus::Ok, "");
    subscription_->updateResources(cluster_names);
    last_cluster_names_ = cluster_names;
  }

  void expectConfigUpdateFailed() override {
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(nullptr));
  }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) override {
    init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(timeout)));
  }

  void expectDisableInitFetchTimeoutTimer() override {
    EXPECT_CALL(*init_timeout_timer_, disableTimer());
  }

  void callInitFetchTimeoutCb() override { init_timeout_timer_->callback_(); }

  const Protobuf::MethodDescriptor* method_descriptor_;
  Grpc::MockAsyncClient* async_client_;
  Event::MockDispatcher dispatcher_;
  Runtime::MockRandomGenerator random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<DeltaEdsSubscriptionImpl> subscription_;
  std::string last_response_nonce_;
  std::vector<std::string> last_cluster_names_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Event::MockTimer* init_timeout_timer_;
  envoy::api::v2::core::Node node_;
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
};

} // namespace
} // namespace Config
} // namespace Envoy