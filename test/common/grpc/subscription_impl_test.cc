#include <algorithm>

#include "common/grpc/subscription_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::Mock;

namespace Envoy {
namespace Grpc {

template class AsyncClientStreamImpl<envoy::api::v2::DiscoveryRequest,
                                     envoy::api::v2::DiscoveryResponse>;

namespace {

typedef MockAsyncClient<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>
    SubscriptionMockAsyncClient;
typedef SubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> EdsSubscriptionImpl;

// TODO(htuch): Move this to a common utility for tests that want proto
// equality.

template <class ProtoType> bool protoEq(ProtoType lhs, ProtoType rhs) {
  return lhs.GetTypeName() == rhs.GetTypeName() &&
         lhs.SerializeAsString() == rhs.SerializeAsString();
}

MATCHER_P(ProtoEq, rhs, "") { return protoEq(arg, rhs); }

MATCHER_P(RepeatedProtoEq, rhs, "") {
  if (arg.size() != rhs.size()) {
    return false;
  }

  for (int i = 0; i < arg.size(); ++i) {
    if (!protoEq(arg[i], rhs[i])) {
      return false;
    }
  }

  return true;
}

class GrpcSubscriptionImplTest : public testing::Test {
public:
  GrpcSubscriptionImplTest()
      : method_descriptor_(envoy::api::v2::EndpointDiscoveryService::descriptor()->FindMethodByName(
            "StreamEndpoints")),
        async_client_(new SubscriptionMockAsyncClient()), timer_(new Event::MockTimer()) {
    node_.set_id("fo0");
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillOnce(Invoke([this](Event::TimerCb timer_cb) {
          timer_cb_ = timer_cb;
          return timer_;
        }));
    subscription_.reset(
        new EdsSubscriptionImpl(node_, std::unique_ptr<SubscriptionMockAsyncClient>(async_client_),
                                dispatcher_, *method_descriptor_));
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) {
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

  void startSubscription(const std::vector<std::string>& cluster_names) {
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
                           bool accept) {
    std::unique_ptr<envoy::api::v2::DiscoveryResponse> response(
        new envoy::api::v2::DiscoveryResponse());
    response->set_version_info(version);
    google::protobuf::RepeatedPtrField<envoy::api::v2::ClusterLoadAssignment> typed_resources;
    for (const auto& cluster : cluster_names) {
      envoy::api::v2::ClusterLoadAssignment* load_assignment = typed_resources.Add();
      load_assignment->set_cluster_name(cluster);
      response->add_resources()->PackFrom(*load_assignment);
    }
    EXPECT_CALL(callbacks_, onConfigUpdate(RepeatedProtoEq(typed_resources)))
        .WillOnce(Return(accept));
    if (accept) {
      expectSendMessage(cluster_names, version);
      version_ = version;
    } else {
      expectSendMessage(cluster_names, version_);
    }
    subscription_->onReceiveMessage(std::move(response));
    Mock::VerifyAndClearExpectations(&async_stream_);
  }

  std::string version_;
  const google::protobuf::MethodDescriptor* method_descriptor_;
  SubscriptionMockAsyncClient* async_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  envoy::api::v2::Node node_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  MockAsyncClientStream<envoy::api::v2::DiscoveryRequest> async_stream_;
  std::unique_ptr<EdsSubscriptionImpl> subscription_;
};

// Validate basic request-response succeeds.
TEST_F(GrpcSubscriptionImplTest, InitialRequestResponse) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that multiple streamed updates succeed.
TEST_F(GrpcSubscriptionImplTest, ResponseStream) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
}

// Validate that the client can reject a config.
TEST_F(GrpcSubscriptionImplTest, RejectConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
}

// Validate that the client can reject a config and accept the same config later.
TEST_F(GrpcSubscriptionImplTest, RejectAcceptConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that the client can reject a config and accept another config later.
TEST_F(GrpcSubscriptionImplTest, RejectAcceptNextConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
}

// Validate that stream updates send a message with the updated resources.
TEST_F(GrpcSubscriptionImplTest, UpdateResources) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  expectSendMessage({"cluster2"}, "0");
  subscription_->updateResources({"cluster2"});
}

// Validate that stream creation results in a timer based retry and can recover.
TEST_F(GrpcSubscriptionImplTest, StreamCreationFailure) {
  InSequence s;
  EXPECT_CALL(*async_client_, start(_, _, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*timer_, enableTimer(_));
  subscription_->start({"cluster0", "cluster1"}, callbacks_);
  // Ensure this doesn't cause an issue by sending a request, since we don't
  // have a gRPC stream.
  subscription_->updateResources({"cluster2"});
  // Retry and succeed.
  EXPECT_CALL(*async_client_, start(_, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({"cluster2"}, "");
  timer_cb_();
}

// Validate that the client can recover from a remote stream closure via retry.
TEST_F(GrpcSubscriptionImplTest, RemoteStreamClose) {
  startSubscription({"cluster0", "cluster1"});
  Http::HeaderMapPtr trailers{new Http::TestHeaderMapImpl{}};
  subscription_->onReceiveTrailingMetadata(std::move(trailers));
  EXPECT_CALL(*timer_, enableTimer(_));
  subscription_->onRemoteClose(Status::GrpcStatus::Canceled);
  // Retry and succeed.
  EXPECT_CALL(*async_client_, start(_, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({"cluster0", "cluster1"}, "");
  timer_cb_();
}

} // namespace
} // namespace Grpc
} // namespace Envoy
