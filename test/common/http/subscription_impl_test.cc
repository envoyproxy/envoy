#include "envoy/http/async_client.h"

#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/http/subscription_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Http {
namespace {

typedef SubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> EdsSubscriptionImpl;

class HttpSubscriptionImplTest : public testing::Test {
public:
  HttpSubscriptionImplTest()
      : method_descriptor_(envoy::api::v2::EndpointDiscoveryService::descriptor()->FindMethodByName(
            "FetchEndpoints")),
        timer_(new Event::MockTimer()), http_request_(&cm_.async_client_) {
    node_.set_id("fo0");
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillOnce(Invoke([this](Event::TimerCb timer_cb) {
          timer_cb_ = timer_cb;
          return timer_;
        }));
    subscription_.reset(new EdsSubscriptionImpl(node_, cm_, "eds_cluster", dispatcher_, random_gen_,
                                                std::chrono::milliseconds(1), *method_descriptor_));
  }

  void TearDown() override {
    // Stop subscribing on the way out.
    if (request_in_progress_) {
      EXPECT_CALL(http_request_, cancel());
    }
    subscription_ = nullptr;
  }

  void expectSendMessage() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("eds_cluster"));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([this](MessagePtr& request, AsyncClient::Callbacks& callbacks,
                                const Optional<std::chrono::milliseconds>& timeout) {
          http_callbacks_ = &callbacks;
          UNREFERENCED_PARAMETER(timeout);
          EXPECT_EQ("POST", std::string(request->headers().Method()->value().c_str()));
          EXPECT_EQ("eds_cluster", std::string(request->headers().Host()->value().c_str()));
          EXPECT_EQ("/v2/discovery:endpoints",
                    std::string(request->headers().Path()->value().c_str()));
          std::string expected_request = "{";
          if (!version_.empty()) {
            expected_request += "\"versionInfo\":\"" + version_ + "\",";
          }
          expected_request += "\"node\":{\"id\":\"fo0\"},";
          if (!cluster_names_.empty()) {
            expected_request +=
                "\"resourceNames\":[\"" + StringUtil::join(cluster_names_, "\",\"") + "\"]";
          }
          expected_request += "}";
          EXPECT_EQ(expected_request, request->bodyAsString());
          request_in_progress_ = true;
          return &http_request_;
        }));
  }

  void startSubscription(const std::vector<std::string>& cluster_names) {
    version_ = "";
    cluster_names_ = cluster_names;
    expectSendMessage();
    subscription_->start(cluster_names, callbacks_);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) {
    std::string response_json = "{\"versionInfo\":\"" + version + "\",\"resources\":[";
    for (const auto& cluster : cluster_names) {
      response_json += "{\"@type\":\"type.googleapis.com/"
                       "envoy.api.v2.ClusterLoadAssignment\",\"clusterName\":\"" +
                       cluster + "\"},";
    }
    response_json.pop_back();
    response_json += "]}";
    envoy::api::v2::DiscoveryResponse response_pb;
    EXPECT_EQ(google::protobuf::util::Status::OK,
              google::protobuf::util::JsonStringToMessage(response_json, &response_pb));
    HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
    MessagePtr message{new ResponseMessageImpl(std::move(response_headers))};
    message->body().reset(new Buffer::OwnedImpl(response_json));
    EXPECT_CALL(callbacks_,
                onConfigUpdate(RepeatedProtoEq(
                    Config::Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(
                        response_pb)))).WillOnce(Return(accept));
    EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
    EXPECT_CALL(*timer_, enableTimer(_));
    http_callbacks_->onSuccess(std::move(message));
    if (accept) {
      version_ = version;
    }
    request_in_progress_ = false;
  }

  void timerTick() {
    expectSendMessage();
    timer_cb_();
  }

  bool request_in_progress_{};
  std::string version_;
  std::vector<std::string> cluster_names_;
  const google::protobuf::MethodDescriptor* method_descriptor_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  envoy::api::v2::Node node_;
  Runtime::MockRandomGenerator random_gen_;
  MockAsyncClientRequest http_request_;
  AsyncClient::Callbacks* http_callbacks_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  std::unique_ptr<EdsSubscriptionImpl> subscription_;
};

// Validate basic fetch succeeds.
TEST_F(HttpSubscriptionImplTest, InitialRequestResponse) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that multiple fetches succeed.
TEST_F(HttpSubscriptionImplTest, ResponseFetches) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  timerTick();
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  timerTick();
}

// Validate that the client can reject a config.
TEST_F(HttpSubscriptionImplTest, RejectConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
}

// Validate that the client can reject a config and accept the same config later.
TEST_F(HttpSubscriptionImplTest, RejectAcceptConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  timerTick();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that the client can reject a config and accept another config later.
TEST_F(HttpSubscriptionImplTest, RejectAcceptNextConfig) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  timerTick();
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
}

// Validate that subscription updates send a message with the updated resources.
TEST_F(HttpSubscriptionImplTest, UpdateResources) {
  startSubscription({"cluster0", "cluster1"});
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  cluster_names_ = {"cluster2"};
  subscription_->updateResources({"cluster2"});
  timerTick();
}

// Validate that the client can recover from a remote fetch failure.
TEST_F(HttpSubscriptionImplTest, OnRequestReset) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_));
  http_callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
  timerTick();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that the client can recover from bad JSON responses.
TEST_F(HttpSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
  MessagePtr message{new ResponseMessageImpl(std::move(response_headers))};
  message->body().reset(new Buffer::OwnedImpl(";!@#badjso n"));
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_));
  http_callbacks_->onSuccess(std::move(message));
  request_in_progress_ = false;
  timerTick();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

} // namespace
} // namespace Http
} // namespace Envoy
