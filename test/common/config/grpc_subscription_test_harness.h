#pragma once

#include "envoy/api/v2/eds.pb.h"

#include "common/common/hash.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/resources.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Config {

typedef GrpcSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> GrpcEdsSubscriptionImpl;

class GrpcSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  GrpcSubscriptionTestHarness()
      : method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints")),
        async_client_(new Grpc::MockAsyncClient()), timer_(new Event::MockTimer()) {
    node_.set_id("fo0");
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    subscription_.reset(
        new GrpcEdsSubscriptionImpl(node_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_),
                                    dispatcher_, *method_descriptor_, stats_));
  }

  ~GrpcSubscriptionTestHarness() { EXPECT_CALL(async_stream_, sendMessage(_, false)); }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) override {
    expectSendMessage(cluster_names, version, Grpc::Status::GrpcStatus::Ok, "");
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names, const std::string& version,
                         const Protobuf::int32 error_code, const std::string& error_message) {
    envoy::api::v2::DiscoveryRequest expected_request;
    expected_request.mutable_node()->CopyFrom(node_);
    for (const auto& cluster : cluster_names) {
      expected_request.add_resource_names(cluster);
    }
    if (!version.empty()) {
      expected_request.set_version_info(version);
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

  void startSubscription(const std::vector<std::string>& cluster_names) override {
    EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
    last_cluster_names_ = cluster_names;
    expectSendMessage(last_cluster_names_, "");
    subscription_->start(cluster_names, callbacks_);
    // These are just there to add coverage to the null implementations of these
    // callbacks.
    Http::HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{}};
    subscription_->grpcMux().onReceiveInitialMetadata(std::move(response_headers));
    Http::TestHeaderMapImpl request_headers;
    subscription_->grpcMux().onCreateInitialMetadata(request_headers);
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept) override {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_version_info(version);
    last_response_nonce_ = std::to_string(HashUtil::xxHash64(version));
    response->set_nonce(last_response_nonce_);
    response->set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
    Protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> typed_resources;
    for (const auto& cluster : cluster_names) {
      if (std::find(last_cluster_names_.begin(), last_cluster_names_.end(), cluster) !=
          last_cluster_names_.end()) {
        envoy::api::v2::ClusterLoadAssignment* load_assignment = typed_resources.Add();
        load_assignment->set_cluster_name(cluster);
        response->add_resources()->PackFrom(*load_assignment);
      }
    }
    EXPECT_CALL(callbacks_, onConfigUpdate(RepeatedProtoEq(typed_resources), version))
        .WillOnce(ThrowOnRejectedConfig(accept));
    if (accept) {
      expectSendMessage(last_cluster_names_, version);
      version_ = version;
    } else {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
      expectSendMessage(last_cluster_names_, version_, Grpc::Status::GrpcStatus::Internal,
                        "bad config");
    }
    subscription_->grpcMux().onReceiveMessage(std::move(response));
    Mock::VerifyAndClearExpectations(&async_stream_);
  }

  void updateResources(const std::vector<std::string>& cluster_names) override {
    std::vector<std::string> cluster_superset = cluster_names;
    cluster_superset.insert(cluster_superset.end(), last_cluster_names_.begin(),
                            last_cluster_names_.end());
    expectSendMessage(cluster_superset, version_);
    expectSendMessage(cluster_names, version_);
    subscription_->updateResources(cluster_names);
    last_cluster_names_ = cluster_names;
  }

  std::string version_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Grpc::MockAsyncClient* async_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  envoy::api::v2::core::Node node_;
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<GrpcEdsSubscriptionImpl> subscription_;
  std::string last_response_nonce_;
  std::vector<std::string> last_cluster_names_;
};

// TODO(danielhochman): test with RDS and ensure version_info is same as what API returned

} // namespace Config
} // namespace Envoy
