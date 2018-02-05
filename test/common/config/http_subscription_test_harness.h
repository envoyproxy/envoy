#include "envoy/api/v2/eds.pb.h"
#include "envoy/http/async_client.h"

#include "common/common/utility.h"
#include "common/config/http_subscription_impl.h"
#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Config {

typedef HttpSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> HttpEdsSubscriptionImpl;

class HttpSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  HttpSubscriptionTestHarness()
      : method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints")),
        timer_(new Event::MockTimer()), http_request_(&cm_.async_client_) {
    node_.set_id("fo0");
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    subscription_.reset(new HttpEdsSubscriptionImpl(node_, cm_, "eds_cluster", dispatcher_,
                                                    random_gen_, std::chrono::milliseconds(1),
                                                    *method_descriptor_, stats_));
  }

  ~HttpSubscriptionTestHarness() {
    // Stop subscribing on the way out.
    if (request_in_progress_) {
      EXPECT_CALL(http_request_, cancel());
    }
  }

  void expectSendMessage(const std::vector<std::string>& cluster_names,
                         const std::string& version) override {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("eds_cluster"));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([this, cluster_names, version](
                             Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                             const Optional<std::chrono::milliseconds>& timeout) {
          http_callbacks_ = &callbacks;
          UNREFERENCED_PARAMETER(timeout);
          EXPECT_EQ("POST", std::string(request->headers().Method()->value().c_str()));
          EXPECT_EQ("eds_cluster", std::string(request->headers().Host()->value().c_str()));
          EXPECT_EQ("/v2/discovery:endpoints",
                    std::string(request->headers().Path()->value().c_str()));
          std::string expected_request = "{";
          if (!version_.empty()) {
            expected_request += "\"version_info\":\"" + version + "\",";
          }
          expected_request += "\"node\":{\"id\":\"fo0\"},";
          if (!cluster_names.empty()) {
            expected_request +=
                "\"resource_names\":[\"" + StringUtil::join(cluster_names, "\",\"") + "\"]";
          }
          expected_request += "}";
          EXPECT_EQ(expected_request, request->bodyAsString());
          request_in_progress_ = true;
          return &http_request_;
        }));
  }

  void startSubscription(const std::vector<std::string>& cluster_names) override {
    version_ = "";
    cluster_names_ = cluster_names;
    expectSendMessage(cluster_names, "");
    subscription_->start(cluster_names, callbacks_);
  }

  void updateResources(const std::vector<std::string>& cluster_names) override {
    cluster_names_ = cluster_names;
    expectSendMessage(cluster_names, version_);
    subscription_->updateResources(cluster_names);
    timer_cb_();
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept) override {
    std::string response_json = "{\"version_info\":\"" + version + "\",\"resources\":[";
    for (const auto& cluster : cluster_names) {
      response_json += "{\"@type\":\"type.googleapis.com/"
                       "envoy.api.v2.ClusterLoadAssignment\",\"cluster_name\":\"" +
                       cluster + "\"},";
    }
    response_json.pop_back();
    response_json += "]}";
    envoy::api::v2::DiscoveryResponse response_pb;
    EXPECT_TRUE(Protobuf::util::JsonStringToMessage(response_json, &response_pb).ok());
    Http::HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
    Http::MessagePtr message{new Http::ResponseMessageImpl(std::move(response_headers))};
    message->body().reset(new Buffer::OwnedImpl(response_json));
    EXPECT_CALL(callbacks_,
                onConfigUpdate(RepeatedProtoEq(
                    Config::Utility::getTypedResources<envoy::api::v2::ClusterLoadAssignment>(
                        response_pb))))
        .WillOnce(ThrowOnRejectedConfig(accept));
    if (!accept) {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
    }
    EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
    EXPECT_CALL(*timer_, enableTimer(_));
    http_callbacks_->onSuccess(std::move(message));
    if (accept) {
      version_ = version;
    }
    EXPECT_EQ(version_, subscription_->versionInfo());
    request_in_progress_ = false;
    timerTick();
  }

  void timerTick() {
    expectSendMessage(cluster_names_, version_);
    timer_cb_();
  }

  bool request_in_progress_{};
  std::string version_;
  std::vector<std::string> cluster_names_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  envoy::api::v2::core::Node node_;
  Runtime::MockRandomGenerator random_gen_;
  Http::MockAsyncClientRequest http_request_;
  Http::AsyncClient::Callbacks* http_callbacks_;
  Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  std::unique_ptr<HttpEdsSubscriptionImpl> subscription_;
};

} // namespace Config
} // namespace Envoy
