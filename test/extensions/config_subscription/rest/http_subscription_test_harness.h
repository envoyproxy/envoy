#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/http/async_client.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/config_subscription/rest/http_subscription_impl.h"

#include "test/common/config/subscription_test_harness.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Config {

class HttpSubscriptionTestHarness : public SubscriptionTestHarness {
public:
  HttpSubscriptionTestHarness() : HttpSubscriptionTestHarness(std::chrono::milliseconds(0)) {}

  HttpSubscriptionTestHarness(std::chrono::milliseconds init_fetch_timeout)
      : method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.endpoint.v3.EndpointDiscoveryService.FetchEndpoints")),
        timer_(new Event::MockTimer()), http_request_(&cm_.thread_local_cluster_.async_client_),
        resource_decoder_(
            std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
                envoy::config::endpoint::v3::ClusterLoadAssignment>>("cluster_name")) {
    node_.set_id("fo0");
    EXPECT_CALL(local_info_, node()).WillOnce(testing::ReturnRef(node_));
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    cm_.initializeThreadLocalClusters({"eds_cluster"});
    EXPECT_CALL(cm_, getThreadLocalCluster("eds_cluster")).Times(AtLeast(0));
    subscription_ = std::make_unique<HttpSubscriptionImpl>(
        local_info_, cm_, "eds_cluster", dispatcher_, random_gen_, std::chrono::milliseconds(1),
        std::chrono::milliseconds(1000), *method_descriptor_,
        Config::TypeUrl::get().ClusterLoadAssignment, callbacks_, resource_decoder_, stats_,
        init_fetch_timeout, validation_visitor_);
  }

  ~HttpSubscriptionTestHarness() override {
    // Stop subscribing on the way out.
    if (request_in_progress_) {
      EXPECT_CALL(http_request_, cancel());
    }
  }

  void expectSendMessage(const std::set<std::string>& cluster_names, const std::string& version,
                         bool expect_node = false) override {
    UNREFERENCED_PARAMETER(expect_node);
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient());
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([this, cluster_names, version](Http::RequestMessagePtr& request,
                                                        Http::AsyncClient::Callbacks& callbacks,
                                                        const Http::AsyncClient::RequestOptions&) {
          http_callbacks_ = &callbacks;
          EXPECT_EQ("POST", request->headers().getMethodValue());
          EXPECT_EQ(Http::Headers::get().ContentTypeValues.Json,
                    request->headers().getContentTypeValue());
          EXPECT_EQ("eds_cluster", request->headers().getHostValue());
          EXPECT_EQ("/v3/discovery:endpoints", request->headers().getPathValue());
          std::string expected_request = "{";
          if (!version_.empty()) {
            expected_request += "\"version_info\":\"" + version + "\",";
          }
          expected_request += "\"node\":{\"id\":\"fo0\"},";
          if (!cluster_names.empty()) {
            std::string joined_cluster_names;
            {
              std::string delimiter = "\",\"";
              std::ostringstream buf;
              std::copy(cluster_names.begin(), cluster_names.end(),
                        std::ostream_iterator<std::string>(buf, delimiter.c_str()));
              std::string with_comma = buf.str();
              joined_cluster_names = with_comma.substr(0, with_comma.length() - delimiter.length());
            }
            expected_request += "\"resource_names\":[\"" + joined_cluster_names + "\"]";
          }
          expected_request += ",\"type_url\":\"type.googleapis.com/"
                              "envoy.config.endpoint.v3.ClusterLoadAssignment\"";
          expected_request += "}";
          EXPECT_EQ(expected_request, request->bodyAsString());
          EXPECT_EQ(fmt::format_int(expected_request.size()).str(),
                    request->headers().getContentLengthValue());
          request_in_progress_ = true;
          return &http_request_;
        }));
  }

  void startSubscription(const std::set<std::string>& cluster_names) override {
    version_ = "";
    cluster_names_ = cluster_names;
    expectSendMessage(cluster_names, "");
    subscription_->start(flattenResources(cluster_names));
  }

  void updateResourceInterest(const std::set<std::string>& cluster_names) override {
    cluster_names_ = cluster_names;
    expectSendMessage(cluster_names, version_);
    subscription_->updateResourceInterest(flattenResources(cluster_names));
    timer_cb_();
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept) override {
    deliverConfigUpdate(cluster_names, version, accept, true, "200");
  }

  void deliverConfigUpdate(const std::vector<std::string>& cluster_names,
                           const std::string& version, bool accept, bool modify,
                           const std::string& response_code) {
    std::string response_json = "{\"version_info\":\"" + version + "\",\"resources\":[";
    for (const auto& cluster : cluster_names) {
      response_json += "{\"@type\":\"type.googleapis.com/"
                       "envoy.config.endpoint.v3.ClusterLoadAssignment\",\"cluster_name\":\"" +
                       cluster + "\"},";
    }
    response_json.pop_back();
    response_json += "]}";
    envoy::service::discovery::v3::DiscoveryResponse response_pb;
    TestUtility::loadFromJson(response_json, response_pb);
    Http::ResponseHeaderMapPtr response_headers{
        new Http::TestResponseHeaderMapImpl{{":status", response_code}}};
    Http::ResponseMessagePtr message{new Http::ResponseMessageImpl(std::move(response_headers))};
    message->body().add(response_json);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::endpoint::v3::ClusterLoadAssignment>(
            response_pb, "cluster_name");

    if (modify) {
      EXPECT_CALL(callbacks_,
                  onConfigUpdate(DecodedResourcesEq(decoded_resources.refvec_), version))
          .WillOnce(ThrowOnRejectedConfig(accept));
    }
    if (!accept) {
      EXPECT_CALL(callbacks_, onConfigUpdateFailed(
                                  Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
    }
    EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
    EXPECT_CALL(*timer_, enableTimer(_, _));
    http_callbacks_->onSuccess(http_request_, std::move(message));
    if (accept) {
      version_ = version;
    }
    request_in_progress_ = false;
    timerTick();
  }

  void expectConfigUpdateFailed() override {
    EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, nullptr));
  }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) override {
    init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(timeout), _));
  }

  void expectDisableInitFetchTimeoutTimer() override {
    EXPECT_CALL(*init_timeout_timer_, disableTimer());
  }

  void callInitFetchTimeoutCb() override { init_timeout_timer_->invokeCallback(); }

  void timerTick() {
    expectSendMessage(cluster_names_, version_);
    timer_cb_();
  }

  bool request_in_progress_{};
  std::string version_;
  std::set<std::string> cluster_names_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;
  envoy::config::core::v3::Node node_;
  Random::MockRandomGenerator random_gen_;
  Http::MockAsyncClientRequest http_request_;
  Http::AsyncClient::Callbacks* http_callbacks_;
  Config::MockSubscriptionCallbacks callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  std::unique_ptr<HttpSubscriptionImpl> subscription_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Event::MockTimer* init_timeout_timer_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

} // namespace Config
} // namespace Envoy
