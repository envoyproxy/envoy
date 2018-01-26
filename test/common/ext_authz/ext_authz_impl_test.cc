#include <chrono>
#include <cstdint>
#include <string>

#include "common/ext_authz/ext_authz_impl.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/address_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/request_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AtLeast;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace ExtAuthz {

class MockRequestCallbacks : public RequestCallbacks {
public:
  MOCK_METHOD1(complete, void(CheckStatus status));
};

class ExtAuthzGrpcClientTest : public testing::Test {
public:
  ExtAuthzGrpcClientTest()
      : async_client_(new Grpc::MockAsyncClient()),
        client_(Grpc::AsyncClientPtr{async_client_}, Optional<std::chrono::milliseconds>()) {}

  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImpl client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;
};

TEST_F(ExtAuthzGrpcClientTest, Basic) {
  std::unique_ptr<envoy::api::v2::auth::CheckResponse> response;

  {
    envoy::api::v2::auth::CheckRequest request;
    Http::HeaderMapImpl headers;
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), Ref(client_), _, _))
        .WillOnce(
            Invoke([this](const Protobuf::MethodDescriptor& service_method,
                          const Protobuf::Message&, Grpc::AsyncRequestCallbacks&, Tracing::Span&,
                          const Optional<std::chrono::milliseconds>&) -> Grpc::AsyncRequest* {
              EXPECT_EQ("envoy.api.v2.auth.Authorization", service_method.service()->full_name());
              EXPECT_EQ("Check", service_method.name());
              return &async_request_;
            }));

    client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

    client_.onCreateInitialMetadata(headers);
    EXPECT_EQ(nullptr, headers.RequestId());

    response.reset(new envoy::api::v2::auth::CheckResponse());
    ::google::rpc::Status* status = new ::google::rpc::Status();
    status->set_code(Grpc::Status::GrpcStatus::PermissionDenied);
    response->set_allocated_status(status);
    EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_unauthorized"));
    EXPECT_CALL(request_callbacks_, complete(CheckStatus::Denied));
    client_.onSuccess(std::move(response), span_);
  }

  {
    envoy::api::v2::auth::CheckRequest request;
    Http::HeaderMapImpl headers;
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

    client_.onCreateInitialMetadata(headers);

    response.reset(new envoy::api::v2::auth::CheckResponse());
    ::google::rpc::Status* status = new ::google::rpc::Status();
    status->set_code(Grpc::Status::GrpcStatus::Ok);
    response->set_allocated_status(status);
    EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_ok"));
    EXPECT_CALL(request_callbacks_, complete(CheckStatus::OK));
    client_.onSuccess(std::move(response), span_);
  }

  {
    envoy::api::v2::auth::CheckRequest request;
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

    response.reset(new envoy::api::v2::auth::CheckResponse());
    EXPECT_CALL(request_callbacks_, complete(CheckStatus::Error));
    client_.onFailure(Grpc::Status::Unknown, "", span_);
  }
}

TEST_F(ExtAuthzGrpcClientTest, Cancel) {
  std::unique_ptr<envoy::api::v2::auth::CheckResponse> response;
  envoy::api::v2::auth::CheckRequest request;

  EXPECT_CALL(*async_client_, send(_, _, _, _, _)).WillOnce(Return(&async_request_));

  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

TEST(ExtAuthzGrpcFactoryTest, Create) {
  envoy::api::v2::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("foo");
  Grpc::MockAsyncClientManager async_client_manager;
  Stats::MockStore scope;
  EXPECT_CALL(async_client_manager, factoryForGrpcService(ProtoEq(config), Ref(scope)))
      .WillOnce(Invoke([](const envoy::api::v2::GrpcService&, Stats::Scope&) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  GrpcFactoryImpl factory(config, async_client_manager, scope);
  factory.create(Optional<std::chrono::milliseconds>());
}

TEST(ExtAuthzNullFactoryTest, Basic) {
  NullFactoryImpl factory;
  ClientPtr client = factory.create(Optional<std::chrono::milliseconds>());
  MockRequestCallbacks request_callbacks;
  envoy::api::v2::auth::CheckRequest request;
  EXPECT_CALL(request_callbacks, complete(CheckStatus::OK));
  client->check(request_callbacks, request, Tracing::NullSpan::instance());
  client->cancel();
}

class ExtAuthzCheckRequestGeneratorTest : public testing::Test {
public:
  ExtAuthzCheckRequestGeneratorTest() {
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
    protocol_ = Envoy::Http::Protocol::Http10;
    // ssl_ = new Envoy::Ssl::MockConnection();
    // Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance("1.2.3.4", 111)};
  };

  Network::Address::InstanceConstSharedPtr addr_;
  Optional<Http::Protocol> protocol_;
  ExtAuthzCheckRequestGenerator check_request_generator_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Envoy::Network::MockReadFilterCallbacks> net_callbacks_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Envoy::Ssl::MockConnection> ssl_;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> req_info_;
};

TEST_F(ExtAuthzCheckRequestGeneratorTest, BasicTcp) {

  envoy::api::v2::auth::CheckRequest request;

  EXPECT_CALL(net_callbacks_, connection()).Times(2).WillRepeatedly(ReturnRef(connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(&ssl_));

  check_request_generator_.createTcpCheck(&net_callbacks_, request);
}

TEST_F(ExtAuthzCheckRequestGeneratorTest, BasicHttp) {

  Http::HeaderMapImpl headers;
  envoy::api::v2::auth::CheckRequest request;

  EXPECT_CALL(callbacks_, connection()).Times(2).WillRepeatedly(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(Const(connection_), ssl()).Times(2).WillRepeatedly(Return(&ssl_));

  EXPECT_CALL(callbacks_, streamId()).WillOnce(Return(0));
  EXPECT_CALL(callbacks_, requestInfo()).Times(2).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, protocol()).WillOnce(ReturnRef(protocol_));
  check_request_generator_.createHttpCheck(&callbacks_, headers, request);
}

} // namespace ExtAuthz
} // namespace Envoy
