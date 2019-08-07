#pragma once

#include "common/config/delta_subscription_impl.h"
#include "common/grpc/common.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Mock;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

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
    subscription_ = std::make_unique<DeltaSubscriptionImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *method_descriptor_, Config::TypeUrl::get().ClusterLoadAssignment, random_, stats_store_,
        rate_limit_settings_, callbacks_, stats_, init_fetch_timeout);
  }

  ~DeltaSubscriptionTestHarness() override {
    while (!nonce_acks_required_.empty()) {
      EXPECT_FALSE(nonce_acks_sent_.empty());
      EXPECT_EQ(nonce_acks_required_.front(), nonce_acks_sent_.front());
      nonce_acks_required_.pop();
      nonce_acks_sent_.pop();
    }
    EXPECT_TRUE(nonce_acks_sent_.empty());
  }

  void startSubscription(const std::set<std::string>& cluster_names) override {
    EXPECT_CALL(*async_client_, startRaw(_, _, _)).WillOnce(Return(&async_stream_));
    last_cluster_names_ = cluster_names;
    expectSendMessage(last_cluster_names_, "");
    subscription_->start(cluster_names);
  }

  void expectSendMessage(const std::set<std::string>& cluster_names,
                         const std::string& version) override {
    UNREFERENCED_PARAMETER(version);
    expectSendMessage(cluster_names, {}, Grpc::Status::GrpcStatus::Ok, "", {});
  }

  void expectSendMessage(const std::set<std::string>& subscribe,
                         const std::set<std::string>& unsubscribe, const Protobuf::int32 error_code,
                         const std::string& error_message,
                         std::map<std::string, std::string> initial_resource_versions) {
    envoy::api::v2::DeltaDiscoveryRequest expected_request;
    expected_request.mutable_node()->CopyFrom(node_);
    std::copy(
        subscribe.begin(), subscribe.end(),
        Protobuf::RepeatedFieldBackInserter(expected_request.mutable_resource_names_subscribe()));
    std::copy(
        unsubscribe.begin(), unsubscribe.end(),
        Protobuf::RepeatedFieldBackInserter(expected_request.mutable_resource_names_unsubscribe()));
    if (!last_response_nonce_.empty()) {
      nonce_acks_required_.push(last_response_nonce_);
      last_response_nonce_ = "";
    }
    expected_request.set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);

    for (auto const& resource : initial_resource_versions) {
      (*expected_request.mutable_initial_resource_versions())[resource.first] = resource.second;
    }

    if (error_code != Grpc::Status::GrpcStatus::Ok) {
      ::google::rpc::Status* error_detail = expected_request.mutable_error_detail();
      error_detail->set_code(error_code);
      error_detail->set_message(error_message);
    }
    EXPECT_CALL(async_stream_,
                sendMessageRaw_(
                    Grpc::ProtoBufferEqIgnoringField(expected_request, "response_nonce"), false))
        .WillOnce([this](Buffer::InstancePtr& buffer, bool) {
          envoy::api::v2::DeltaDiscoveryRequest message;
          EXPECT_TRUE(Grpc::Common::parseBufferInstance(std::move(buffer), message));
          const std::string nonce = message.response_nonce();
          if (!nonce.empty()) {
            nonce_acks_sent_.push(nonce);
          }
        });
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
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(
                                  Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
      expectSendMessage({}, {}, Grpc::Status::GrpcStatus::Internal, "bad config", {});
    }
    subscription_->onDiscoveryResponse(std::move(response));
    Mock::VerifyAndClearExpectations(&async_stream_);
  }

  void updateResources(const std::set<std::string>& cluster_names) override {
    std::set<std::string> sub;
    std::set<std::string> unsub;

    std::set_difference(cluster_names.begin(), cluster_names.end(), last_cluster_names_.begin(),
                        last_cluster_names_.end(), std::inserter(sub, sub.begin()));
    std::set_difference(last_cluster_names_.begin(), last_cluster_names_.end(),
                        cluster_names.begin(), cluster_names.end(),
                        std::inserter(unsub, unsub.begin()));

    expectSendMessage(sub, unsub, Grpc::Status::GrpcStatus::Ok, "", {});
    subscription_->updateResources(cluster_names);
    last_cluster_names_ = cluster_names;
  }

  void expectConfigUpdateFailed() override {
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, nullptr));
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
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<DeltaSubscriptionImpl> subscription_;
  std::string last_response_nonce_;
  std::set<std::string> last_cluster_names_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Event::MockTimer* init_timeout_timer_;
  envoy::api::v2::core::Node node_;
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
  std::queue<std::string> nonce_acks_required_;
  std::queue<std::string> nonce_acks_sent_;
};

} // namespace
} // namespace Config
} // namespace Envoy
