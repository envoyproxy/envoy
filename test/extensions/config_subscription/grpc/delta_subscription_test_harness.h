#pragma once

#include <queue>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/grpc/common.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_impl.h"
#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/xds_mux/grpc_mux_impl.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/common.h"
#include "test/mocks/config/custom_config_validators.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
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
  DeltaSubscriptionTestHarness(Envoy::Config::LegacyOrUnified legacy_or_unified)
      : DeltaSubscriptionTestHarness(legacy_or_unified, std::chrono::milliseconds(0)) {}
  DeltaSubscriptionTestHarness(Envoy::Config::LegacyOrUnified legacy_or_unified,
                               std::chrono::milliseconds init_fetch_timeout)
      : method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.endpoint.v3.EndpointDiscoveryService.StreamEndpoints")),
        async_client_(new Grpc::MockAsyncClient()),
        resource_decoder_(std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
                              envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name")),
        should_use_unified_(legacy_or_unified == Envoy::Config::LegacyOrUnified::Unified) {
    node_.set_id("fo0");
    EXPECT_CALL(local_info_, node()).WillRepeatedly(testing::ReturnRef(node_));
    EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
    auto backoff_strategy = std::make_unique<JitteredExponentialBackOffStrategy>(
        SubscriptionFactory::RetryInitialDelayMs, SubscriptionFactory::RetryMaxDelayMs, random_);
    GrpcMuxContext grpc_mux_context{
        /*async_client_=*/std::unique_ptr<Grpc::MockAsyncClient>(async_client_),
        /*failover_async_client_=*/nullptr,
        /*dispatcher_=*/dispatcher_,
        /*service_method_=*/*method_descriptor_,
        /*local_info_=*/local_info_,
        /*rate_limit_settings_=*/rate_limit_settings_,
        /*scope_=*/*stats_store_.rootScope(),
        /*config_validators_=*/std::make_unique<NiceMock<MockCustomConfigValidators>>(),
        /*xds_resources_delegate_=*/XdsResourcesDelegateOptRef(),
        /*xds_config_tracker_=*/XdsConfigTrackerOptRef(),
        /*backoff_strategy_=*/std::move(backoff_strategy),
        /*target_xds_authority_=*/"",
        /*eds_resources_cache_=*/nullptr};
    if (should_use_unified_) {
      xds_context_ = std::make_shared<Config::XdsMux::GrpcMuxDelta>(grpc_mux_context, false);
    } else {
      xds_context_ = std::make_shared<NewGrpcMuxImpl>(grpc_mux_context);
    }
    subscription_ = std::make_unique<GrpcSubscriptionImpl>(
        xds_context_, callbacks_, resource_decoder_, stats_,
        Config::TypeUrl::get().ClusterLoadAssignment, dispatcher_, init_fetch_timeout, false,
        SubscriptionOptions());
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  }

  void doSubscriptionTearDown() override {
    if (subscription_started_) {
      EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
      subscription_.reset();
    }
  }

  ~DeltaSubscriptionTestHarness() override {
    while (!nonce_acks_required_.empty()) {
      if (nonce_acks_sent_.empty()) {
        // It's not enough to EXPECT_FALSE(nonce_acks_sent_.empty()), we need to skip the following
        // EXPECT_EQ, otherwise the undefined .front() can get pretty bad.
        EXPECT_FALSE(nonce_acks_sent_.empty());
        break;
      }
      EXPECT_EQ(nonce_acks_required_.front(), nonce_acks_sent_.front());
      nonce_acks_required_.pop();
      nonce_acks_sent_.pop();
    }
    EXPECT_TRUE(nonce_acks_sent_.empty());
  }

  void startSubscription(const std::set<std::string>& cluster_names) override {
    subscription_started_ = true;
    last_cluster_names_ = cluster_names;
    expectSendMessage(last_cluster_names_, "");
    subscription_->start(flattenResources(cluster_names));
  }

  void expectSendMessage(const std::set<std::string>& cluster_names, const std::string& version,
                         bool expect_node = false) override {
    UNREFERENCED_PARAMETER(version);
    UNREFERENCED_PARAMETER(expect_node);
    expectSendMessage(cluster_names, {}, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
  }

  void expectSendMessage(const std::set<std::string>& subscribe,
                         const std::set<std::string>& unsubscribe, const Protobuf::int32 error_code,
                         const std::string& error_message,
                         std::map<std::string, std::string> initial_resource_versions) {
    API_NO_BOOST(envoy::service::discovery::v3::DeltaDiscoveryRequest) expected_request;
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

    if (error_code != Grpc::Status::WellKnownGrpcStatus::Ok) {
      ::google::rpc::Status* error_detail = expected_request.mutable_error_detail();
      error_detail->set_code(error_code);
      error_detail->set_message(error_message);
    }
    EXPECT_CALL(async_stream_,
                sendMessageRaw_(
                    Grpc::ProtoBufferEqIgnoringField(expected_request, "response_nonce"), false))
        .WillOnce([this](Buffer::InstancePtr& buffer, bool) {
          API_NO_BOOST(envoy::service::discovery::v3::DeltaDiscoveryRequest) message;
          EXPECT_TRUE(Grpc::Common::parseBufferInstance(std::move(buffer), message));
          const std::string nonce = message.response_nonce();
          if (!nonce.empty()) {
            nonce_acks_sent_.push(nonce);
          }
        });
  }

  void onDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryResponse>&& response) {
    if (should_use_unified_) {
      dynamic_cast<XdsMux::GrpcMuxDelta*>(subscription_->grpcMux().get())
          ->onDiscoveryResponse(std::move(response), control_plane_stats_);
    } else {
      dynamic_cast<NewGrpcMuxImpl*>(subscription_->grpcMux().get())
          ->onDiscoveryResponse(std::move(response), control_plane_stats_);
    }
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept) override {
    auto response = std::make_unique<envoy::service::discovery::v3::DeltaDiscoveryResponse>();
    last_response_nonce_ = std::to_string(HashUtil::xxHash64(version));
    response->set_nonce(last_response_nonce_);
    response->set_system_version_info(version);
    response->set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);

    Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::ClusterLoadAssignment> typed_resources;
    for (const auto& cluster : cluster_names) {
      if (std::find(last_cluster_names_.begin(), last_cluster_names_.end(), cluster) !=
          last_cluster_names_.end()) {
        envoy::config::endpoint::v3::ClusterLoadAssignment* load_assignment = typed_resources.Add();
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
      expectSendMessage({}, {}, Grpc::Status::WellKnownGrpcStatus::Internal, "bad config", {});
    }
    onDiscoveryResponse(std::move(response));
    Mock::VerifyAndClearExpectations(&async_stream_);
  }

  void updateResourceInterest(const std::set<std::string>& cluster_names) override {
    std::set<std::string> sub;
    std::set<std::string> unsub;

    std::set_difference(cluster_names.begin(), cluster_names.end(), last_cluster_names_.begin(),
                        last_cluster_names_.end(), std::inserter(sub, sub.begin()));
    std::set_difference(last_cluster_names_.begin(), last_cluster_names_.end(),
                        cluster_names.begin(), cluster_names.end(),
                        std::inserter(unsub, unsub.begin()));

    expectSendMessage(sub, unsub, Grpc::Status::WellKnownGrpcStatus::Ok, "", {});
    subscription_->updateResourceInterest(flattenResources(cluster_names));
    last_cluster_names_ = cluster_names;
  }

  void expectConfigUpdateFailed() override {
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, nullptr));
  }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) override {
    init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*init_timeout_timer_, enableTimer(timeout, _));
  }

  void expectDisableInitFetchTimeoutTimer() override {
    EXPECT_CALL(*init_timeout_timer_, disableTimer());
  }

  void callInitFetchTimeoutCb() override { init_timeout_timer_->invokeCallback(); }

  const Protobuf::MethodDescriptor* method_descriptor_;
  Grpc::MockAsyncClient* async_client_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncStream async_stream_;
  GrpcMuxSharedPtr xds_context_;
  GrpcSubscriptionImplPtr subscription_;
  std::string last_response_nonce_;
  std::set<std::string> last_cluster_names_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Event::MockTimer* init_timeout_timer_;
  envoy::config::core::v3::Node node_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  std::queue<std::string> nonce_acks_required_;
  std::queue<std::string> nonce_acks_sent_;
  bool subscription_started_{};
  bool should_use_unified_;
};

} // namespace
} // namespace Config
} // namespace Envoy
