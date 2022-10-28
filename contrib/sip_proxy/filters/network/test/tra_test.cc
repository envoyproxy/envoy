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

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

class SipTraTest : public testing::Test {
public:
  SipTraTest() : stream_info_(time_source_, nullptr) {}
  std::shared_ptr<SipProxy::MockTrafficRoutingAssistantHandlerDeep> initTraHandler() {
    std::string tra_yaml = R"EOF(
               grpc_service:
                 envoy_grpc:
                   cluster_name: tra_service
               timeout: 2s
               transport_api_version: V3
)EOF";

    auto tra_config = std::make_shared<
        envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig>();
    TestUtility::loadFromYaml(tra_yaml, *tra_config);

    SipFilterStats stat = SipFilterStats::generateStats("test.", store_);
    auto config = std::make_shared<NiceMock<MockConfig>>();
    EXPECT_CALL(*config, stats()).WillRepeatedly(ReturnRef(stat));
    auto context = std::make_shared<NiceMock<Server::Configuration::MockFactoryContext>>();
    auto filter = std::make_shared<NiceMock<MockConnectionManager>>(*config, random_, time_source_,
                                                                    *context, nullptr);

    auto tra_handler = std::make_shared<NiceMock<SipProxy::MockTrafficRoutingAssistantHandlerDeep>>(
        *filter, dispatcher_, *tra_config, *context, stream_info_);

    auto async_client = std::make_shared<testing::NiceMock<Grpc::MockAsyncClient>>();

    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillRepeatedly(Return(async_client->async_request_.get()));

    async_stream_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncStream>>();
    EXPECT_CALL(*async_client, startRaw(_, _, _, _)).WillRepeatedly(Return(async_stream_.get()));

    auto grpc_client = std::make_unique<TrafficRoutingAssistant::GrpcClientImpl>(
        async_client, dispatcher_, std::chrono::milliseconds(2000));
    tra_client_ = std::move(grpc_client);
    EXPECT_CALL(*tra_handler, traClient()).WillRepeatedly(ReturnRef(tra_client_));
    return tra_handler;
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockTimeSystem> time_source_;
  Tracing::MockSpan span_;
  Stats::TestUtil::TestStore store_;
  NiceMock<Random::MockRandomGenerator> random_;
  StreamInfo::StreamInfoImpl stream_info_;
  std::unique_ptr<testing::NiceMock<Grpc::MockAsyncStream>> async_stream_;
  TrafficRoutingAssistant::ClientPtr tra_client_;
};

TEST_F(SipTraTest, TraUpdate) {
  auto tra_handler = initTraHandler();
  tra_handler->updateTrafficRoutingAssistant("lskpmc", "S2F1", "10.0.0.1", absl::nullopt);
}

TEST_F(SipTraTest, TraUpdateWithSIPContext) {
  auto tra_handler = initTraHandler();
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->setMethodType(MethodType::Register);
  metadata->addMsgHeader(HeaderType::From, "user@sip.com");
  tra_handler->updateTrafficRoutingAssistant("lskpmc", "S2F1", "10.0.0.1", metadata->traContext());
}

TEST_F(SipTraTest, TraRetrieveContinue) {
  auto tra_handler = initTraHandler();
  tra_handler->updateTrafficRoutingAssistant("lskpmc", "S1F1", "10.0.0.1", absl::nullopt);

  NiceMock<SipFilters::MockDecoderFilterCallbacks> callbacks;
  std::string host = "";
  EXPECT_EQ(QueryStatus::Continue, tra_handler->retrieveTrafficRoutingAssistant(
                                       "lskpmc", "S1F1", absl::nullopt, callbacks, host));
  EXPECT_EQ(host, "10.0.0.1");
}

TEST_F(SipTraTest, TraRetrievePending) {
  auto tra_handler = initTraHandler();
  NiceMock<SipFilters::MockDecoderFilterCallbacks> callbacks;

  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->setMethodType(MethodType::Register);
  metadata->addMsgHeader(HeaderType::From, "user@sip.com");
  metadata->affinity().emplace_back("Route", "lskpmc", "", true, true);
  metadata->resetAffinityIteration();
  EXPECT_CALL(callbacks, metadata()).WillRepeatedly(Return(metadata));
  std::string host = "";
  EXPECT_EQ(QueryStatus::Pending, tra_handler->retrieveTrafficRoutingAssistant(
                                      "lskpmc", "S1F1", metadata->traContext(), callbacks, host));
  EXPECT_EQ(host, "");
}

TEST_F(SipTraTest, TraRetrieveStop) {
  auto tra_handler = initTraHandler();
  NiceMock<SipFilters::MockDecoderFilterCallbacks> callbacks;

  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->setMethodType(MethodType::Register);
  metadata->addMsgHeader(HeaderType::From, "user@sip.com");
  metadata->affinity().emplace_back("Route", "lskpmc", "", false, true);
  metadata->resetAffinityIteration();
  EXPECT_CALL(callbacks, metadata()).WillRepeatedly(Return(metadata));
  std::string host = "10.0.0.1";
  EXPECT_EQ(QueryStatus::Stop, tra_handler->retrieveTrafficRoutingAssistant(
                                   "lskpmc", "S1F1", metadata->traContext(), callbacks, host));
}

TEST_F(SipTraTest, TraCompleteUpdateRsp) {
  auto tra_handler = initTraHandler();
  tra_handler->complete(TrafficRoutingAssistant::ResponseType::UpdateResp, "", "");
}

TEST_F(SipTraTest, TraCompleteCreateRsp) {
  auto tra_handler = initTraHandler();
  tra_handler->complete(TrafficRoutingAssistant::ResponseType::CreateResp, "", "");
}

TEST_F(SipTraTest, TraCompleteDeleteRsp) {
  auto tra_handler = initTraHandler();
  tra_handler->complete(TrafficRoutingAssistant::ResponseType::DeleteResp, "", "");
}

TEST_F(SipTraTest, TraCompleteRetrieveRsp) {
  auto tra_handler = initTraHandler();
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::RetrieveResponse
      retrive_response_config;
  std::string retrieveRsp_yaml = R"EOF(
             data: {"S1F1":"10.0.0.1"}
)EOF";
  TestUtility::loadFromYaml(retrieveRsp_yaml, retrive_response_config);

  tra_handler->complete(TrafficRoutingAssistant::ResponseType::RetrieveResp, "lskpmc",
                        retrive_response_config);
}

TEST_F(SipTraTest, TraCompleteSubscribeRsp) {
  auto tra_handler = initTraHandler();
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::SubscribeResponse
      subscribe_response_config;
  std::string subscribeRsp_yaml = R"EOF(
             data: {"S2F1":"10.0.0.1"}
)EOF";
  TestUtility::loadFromYaml(subscribeRsp_yaml, subscribe_response_config);

  tra_handler->complete(TrafficRoutingAssistant::ResponseType::SubscribeResp, "lskpmc",
                        subscribe_response_config);
}

TEST_F(SipTraTest, TraDoSubscribe) {
  auto tra_handler = initTraHandler();
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
  tra_handler->doSubscribe(affinity_config);
}

TEST_F(SipTraTest, TraDelete) {
  auto tra_handler = initTraHandler();
  tra_handler->deleteTrafficRoutingAssistant("lskpmc", "S1F1", absl::nullopt);
}

TEST_F(SipTraTest, TraDeleteWithSIPContext) {
  auto tra_handler = initTraHandler();
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->setMethodType(MethodType::Bye);
  metadata->addMsgHeader(HeaderType::From, "user@sip.com");
  tra_handler->deleteTrafficRoutingAssistant("lskpmc", "S1F1", metadata->traContext());
}

TEST_F(SipTraTest, TraSubscribe) {
  auto tra_handler = initTraHandler();
  tra_handler->subscribeTrafficRoutingAssistant("lskpmc");
}

TEST_F(SipTraTest, GrpcClientOnSuccessRetrieveRsp) {
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
            retrieve_response:
              data: {"S1F1": "10.0.0.1"}
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
          service_response_config);

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(nullptr, dispatcher_, absl::nullopt);
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);
  grpc_client.onSuccess(std::move(response), span_);
}

TEST_F(SipTraTest, GrpcClientOnSuccessCreateRsp) {
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  auto create_response =
      new envoy::extensions::filters::network::sip_proxy::tra::v3alpha::CreateResponse();
  service_response_config.set_allocated_create_response(create_response);
  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
          service_response_config);

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(nullptr, dispatcher_, absl::nullopt);
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);
  grpc_client.onSuccess(std::move(response), span_);
}

TEST_F(SipTraTest, GrpcClientOnSuccessUpdateRsp) {
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  auto update_response =
      new envoy::extensions::filters::network::sip_proxy::tra::v3alpha::UpdateResponse();
  service_response_config.set_allocated_update_response(update_response);
  auto response = std::make_unique<
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
      service_response_config);

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(nullptr, dispatcher_, absl::nullopt);
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);
  grpc_client.onSuccess(std::move(response), span_);
}

TEST_F(SipTraTest, GrpcClientOnSuccessDeleteRsp) {
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse
      service_response_config;
  std::string serviceRsp_yaml = R"EOF(
            type: lskpmc
            ret: 0
            reason: success
)EOF";
  TestUtility::loadFromYaml(serviceRsp_yaml, service_response_config);

  auto delete_response =
      new envoy::extensions::filters::network::sip_proxy::tra::v3alpha::DeleteResponse();
  service_response_config.set_allocated_delete_response(delete_response);
  auto response = std::make_unique<
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>(
      service_response_config);

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(nullptr, dispatcher_, absl::nullopt);
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);
  grpc_client.onSuccess(std::move(response), span_);
}

TEST_F(SipTraTest, GrpcClientOnReceiveMessage) {
  std::unique_ptr<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      response = std::make_unique<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>();

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(nullptr, dispatcher_, absl::nullopt);
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);
  grpc_client.onReceiveMessage(std::move(response));
}

TEST_F(SipTraTest, GrpcClientOnFailure) {
  Grpc::Status::GrpcStatus status = Grpc::Status::WellKnownGrpcStatus::Unknown;

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(nullptr, dispatcher_, absl::nullopt);
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);
  grpc_client.onFailure(status, "", span_);
}

TEST_F(SipTraTest, Misc) {
  auto async_client = std::make_shared<testing::NiceMock<Grpc::MockAsyncClient>>();
  Grpc::RawAsyncRequestCallbacks* request_cb;
  Grpc::RawAsyncStreamCallbacks* stream_cb;

  EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
      .WillRepeatedly(Invoke([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                                 Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span&,
                                 const Http::AsyncClient::RequestOptions&) {
        request_cb = &callbacks;
        return async_client->async_request_.get();
      }));

  async_stream_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncStream>>();
  EXPECT_CALL(*async_client, startRaw(_, _, _, _))
      .WillRepeatedly(
          Invoke([&](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                     const Http::AsyncClient::StreamOptions&) {
            stream_cb = &callbacks;
            return async_stream_.get();
          }));

  auto grpc_client = TrafficRoutingAssistant::GrpcClientImpl(async_client, dispatcher_,
                                                             std::chrono::milliseconds(2000));
  NiceMock<SipProxy::MockRequestCallbacks> request_callbacks;
  grpc_client.setRequestCallbacks(request_callbacks);

  absl::flat_hash_map<std::string, std::string> data;
  data.emplace(std::make_pair("S1F1", "10.0.0.1"));
  grpc_client.createTrafficRoutingAssistant("lskpmc", data, absl::nullopt, span_, stream_info_);

  Http::TestRequestHeaderMapImpl request_headers;
  request_cb->onCreateInitialMetadata(request_headers);
  request_cb->onSuccessRaw(std::make_unique<Buffer::OwnedImpl>(""), span_);

  Grpc::Status::GrpcStatus status = Grpc::Status::WellKnownGrpcStatus::Unknown;
  request_cb->onFailure(status, "", span_);

  grpc_client.subscribeTrafficRoutingAssistant("lskpmc", span_, stream_info_);

  Http::TestRequestHeaderMapImpl request_headers2;
  stream_cb->onCreateInitialMetadata(request_headers2);

  Http::ResponseHeaderMapPtr response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
  stream_cb->onReceiveInitialMetadata(std::move(response_headers));

  Http::ResponseTrailerMapPtr response_trailers =
      std::make_unique<Http::TestResponseTrailerMapImpl>();
  stream_cb->onReceiveTrailingMetadata(std::move(response_trailers));
  stream_cb->onReceiveMessageRaw(std::make_unique<Buffer::OwnedImpl>(""));
  stream_cb->onRemoteClose(status, "");
}

TEST_F(SipTraTest, TraGetTraContextFromMetadata) {
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>("");
  metadata->setMethodType(MethodType::Register);
  metadata->addMsgHeader(HeaderType::From, "user@sip.com");

  auto context = metadata->traContext();
  EXPECT_EQ(context["from_header"], "user@sip.com");
  EXPECT_EQ(context["method_type"], "REGISTER");

  auto context2 = metadata->traContext();
  EXPECT_EQ(context2, context);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
