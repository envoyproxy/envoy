#include <chrono>
#include <memory>

#include "source/common/buffer/buffer_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha/tra.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/config.h"
#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
#include "contrib/sip_proxy/filters/network/source/tra/tra_impl.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "contrib/sip_proxy/filters/network/test/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

class SipTraTest : public testing::Test {
public:
  SipTraTest() : async_stream_(std::make_unique<testing::NiceMock<Grpc::MockAsyncStream>>()) {}
  ~SipTraTest() override { delete (filter_); }

  void initTraHandler() {
    std::string sip_proxy_yaml = R"EOF(
           stat_prefix: egress_sip
           route_config:
             routes:
             - match:
                domain: "icscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster2
           settings:
             transaction_timeout: 32s
             local_services:
             - domain: "pcsf-cfed.cncs.svc.cluster.local"
               parameter: "x-suri"
)EOF";
    TestUtility::loadFromYaml(sip_proxy_yaml, sip_proxy_config_);

    std::string tra_yaml = R"EOF(
               grpc_service:
                 envoy_grpc:
                   cluster_name: tra_service
               timeout: 2s
               transport_api_version: V3
)EOF";
    TestUtility::loadFromYaml(tra_yaml, tra_config_);

    StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
    SipFilterStats stat = SipFilterStats::generateStats("test.", store_);
    EXPECT_CALL(config_, stats()).WillRepeatedly(ReturnRef(stat));
    filter_ =
        new NiceMock<MockConnectionManager>(config_, random_, time_source_, context_, nullptr);

    tra_handler_ = std::make_shared<NiceMock<SipProxy::MockTrafficRoutingAssistantHandlerDeep>>(
        *filter_, sip_proxy_config_.settings().tra_service_config(), context_, stream_info);
  }

  void initGrpcClient() {
    initTraHandler();
    async_request_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncRequest>>();
    async_stream_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncStream>>();
    async_client_ = std::make_shared<testing::NiceMock<Grpc::MockAsyncClient>>();
    EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillRepeatedly(testing::Return(async_request_.get()));
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
        .WillRepeatedly(testing::Return(async_stream_.get()));

    grpc_client_ = std::make_unique<TrafficRoutingAssistant::MockGrpcClientImpl>(
        async_client_, std::chrono::milliseconds(2000));
    grpc_client_->setRequestCallbacks(*tra_handler_);
  }

  void initTraClient() {
    initGrpcClient();
    tra_client_ = std::move(grpc_client_);
    EXPECT_CALL(*tra_handler_, traClient()).WillRepeatedly(ReturnRef(tra_client_));
  }

  NiceMock<MockConnectionManager>* filter_{};
  NiceMock<MockTimeSystem> time_source_;
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy sip_proxy_config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<MockConfig> config_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore store_;
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig tra_config_;
  NiceMock<SipFilters::MockDecoderFilterCallbacks> callbacks_;
  Tracing::MockSpan span_;

  std::unique_ptr<testing::NiceMock<Grpc::MockAsyncRequest>> async_request_;
  std::unique_ptr<testing::NiceMock<Grpc::MockAsyncStream>> async_stream_;
  std::unique_ptr<TrafficRoutingAssistant::MockGrpcClientImpl> grpc_client_;
  std::shared_ptr<SipProxy::MockTrafficRoutingAssistantHandlerDeep> tra_handler_;
  std::shared_ptr<Grpc::MockAsyncClient> async_client_;
  TrafficRoutingAssistant::ClientPtr tra_client_;
};

TEST_F(SipTraTest, TraUpdate) {
  initTraClient();
  tra_handler_->updateTrafficRoutingAssistant("lskpmc", "S1F1", "10.0.0.1");
}

TEST_F(SipTraTest, TraRetrieveContinue) {
  initTraClient();
  tra_handler_->updateTrafficRoutingAssistant("lskpmc", "S1F1", "10.0.0.1");

  std::string host = "10.0.0.1";
  EXPECT_EQ(QueryStatus::Continue,
            tra_handler_->retrieveTrafficRoutingAssistant("lskpmc", "S1F1", callbacks_, host));
}

TEST_F(SipTraTest, TraRetrievePending) {
  initTraClient();
  StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->addQuery("lskpmc", true);
  EXPECT_CALL(callbacks_, metadata()).WillRepeatedly(Return(metadata));
  std::string host = "10.0.0.1";
  EXPECT_EQ(QueryStatus::Pending,
            tra_handler_->retrieveTrafficRoutingAssistant("lskpmc", "S1F1", callbacks_, host));
}

TEST_F(SipTraTest, TraRetrieveStop) {
  initTraClient();
  StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->addQuery("lskpmc", false);
  EXPECT_CALL(callbacks_, metadata()).WillRepeatedly(Return(metadata));
  std::string host = "10.0.0.1";
  EXPECT_EQ(QueryStatus::Stop,
            tra_handler_->retrieveTrafficRoutingAssistant("lskpmc", "S1F1", callbacks_, host));
}

TEST_F(SipTraTest, TraCompleteUpdateRsp) {
  initTraClient();
  tra_handler_->complete(TrafficRoutingAssistant::ResponseType::UpdateResp, "", "");
}

TEST_F(SipTraTest, TraCompleteCreateRsp) {
  initTraClient();
  tra_handler_->complete(TrafficRoutingAssistant::ResponseType::CreateResp, "", "");
}

TEST_F(SipTraTest, TraCompleteDeleteRsp) {
  initTraClient();
  tra_handler_->complete(TrafficRoutingAssistant::ResponseType::DeleteResp, "", "");
}

TEST_F(SipTraTest, TraCompleteRetrieveRsp) {
  initTraClient();
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::RetrieveResponse
      retrive_response_config;
  std::string retrieveRsp_yaml = R"EOF(
            data: {"S1F1":"10.0.0.1"}
)EOF";
  TestUtility::loadFromYaml(retrieveRsp_yaml, retrive_response_config);

  tra_handler_->complete(TrafficRoutingAssistant::ResponseType::RetrieveResp, "lskpmc",
                         retrive_response_config);
}

TEST_F(SipTraTest, TraCompleteSubscribeRsp) {
  initTraClient();
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::SubscribeResponse
      subscribe_response_config;
  std::string subscribeRsp_yaml = R"EOF(
            data: {"S1F1":"10.0.0.1"}
)EOF";
  TestUtility::loadFromYaml(subscribeRsp_yaml, subscribe_response_config);

  tra_handler_->complete(TrafficRoutingAssistant::ResponseType::SubscribeResp, "lskpmc",
                         subscribe_response_config);
}

TEST_F(SipTraTest, TraDoSubscribe) {
  initTraClient();
  envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity affinity_config;
  std::string affinity_yaml = R"EOF(
           entries:
           - key_name: lskpmc
             query: true
             subscribe: true
           - key_name: ep
             query: false
             subscribe: false
)EOF";
  TestUtility::loadFromYaml(affinity_yaml, affinity_config);

  tra_handler_->doSubscribe(affinity_config);
}

TEST_F(SipTraTest, TraDelete) {
  initTraClient();
  tra_handler_->deleteTrafficRoutingAssistant("lskpmc", "S1F1");
}

TEST_F(SipTraTest, TraSubscribe) {
  initTraClient();
  tra_handler_->subscribeTrafficRoutingAssistant("lskpmc");
}

TEST_F(SipTraTest, GrpcClientCancel) {
  initGrpcClient();
  StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
  absl::flat_hash_map<std::string, std::string> data;
  data.emplace(std::make_pair("S1F1", "10.0.0.1"));
  grpc_client_->createTrafficRoutingAssistant("lskpmc", data, span_, stream_info);
  grpc_client_->cancel();
}

TEST_F(SipTraTest, GrpcClientCloseStream) {
  initGrpcClient();
  StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
  grpc_client_->subscribeTrafficRoutingAssistant("lskpmc", span_, stream_info);

  grpc_client_->closeStream();
}

TEST_F(SipTraTest, GrpcClientOnSuccessRetrieveRsp) {
  initGrpcClient();

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
            retrieve_response:
              data: {"S1F1", "10.0.0.1"}
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
          service_response_config);
  grpc_client_->onSuccess(std::move(response), span_);
}

TEST_F(SipTraTest, GrpcClientOnSuccessCreateRsp) {
  initGrpcClient();

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::CreateResponse* create_response =
      new envoy::extensions::filters::network::sip_proxy::tra::v3alpha::CreateResponse();
  service_response_config.set_allocated_create_response(create_response);
  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
          service_response_config);
  grpc_client_->onSuccess(std::move(response), span_);
  delete (create_response);
}

TEST_F(SipTraTest, GrpcClientOnSuccessUpdateRsp) {
  initGrpcClient();

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::UpdateResponse* update_response =
      new envoy::extensions::filters::network::sip_proxy::tra::v3alpha::UpdateResponse();
  service_response_config.set_allocated_update_response(update_response);
  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
          service_response_config);
  grpc_client_->onSuccess(std::move(response), span_);
  delete (update_response);
}

TEST_F(SipTraTest, GrpcClientOnSuccessDeleteRsp) {
  initGrpcClient();

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::DeleteResponse* delete_response =
      new envoy::extensions::filters::network::sip_proxy::tra::v3alpha::DeleteResponse();
  service_response_config.set_allocated_delete_response(delete_response);
  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
          service_response_config);
  grpc_client_->onSuccess(std::move(response), span_);
  delete (delete_response);
}

TEST_F(SipTraTest, GrpcClientOnReceiveMessage) {
  initGrpcClient();

  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>();
  grpc_client_->onReceiveMessage(std::move(response));
}

TEST_F(SipTraTest, GrpcClientOnFailure) {
  initGrpcClient();
  Grpc::Status::GrpcStatus status = Grpc::Status::WellKnownGrpcStatus::Ok;
  grpc_client_->onFailure(status, "", span_);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
