#include "envoy/stats/scope.h"

#include "common/api/api_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/grpc/google_async_client_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class MockGenericStub : public GoogleStub {
public:
  MOCK_METHOD3(PrepareCall_, grpc::GenericClientAsyncReaderWriter*(grpc::ClientContext* context,
                                                                   const grpc::string& method,
                                                                   grpc::CompletionQueue* cq));

  std::unique_ptr<grpc::GenericClientAsyncReaderWriter>
  PrepareCall(grpc::ClientContext* context, const grpc::string& method,
              grpc::CompletionQueue* cq) override {
    return std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(PrepareCall_(context, method, cq));
  }
};

class MockStubFactory : public GoogleStubFactory {
public:
  std::shared_ptr<GoogleStub> createStub(std::shared_ptr<grpc::Channel> /*channel*/) override {
    return shared_stub_;
  }

  MockGenericStub* stub_ = new MockGenericStub();
  std::shared_ptr<GoogleStub> shared_stub_{stub_};
};

class EnvoyGoogleAsyncClientImplTest : public testing::Test {
public:
  EnvoyGoogleAsyncClientImplTest()
      : dispatcher_(test_time_.timeSystem()), stats_store_(new Stats::IsolatedStoreImpl),
        api_(Api::createApiForTest(*stats_store_)), scope_(stats_store_),
        method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {
    envoy::api::v2::core::GrpcService config;
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->set_target_uri("fake_address");
    google_grpc->set_stat_prefix("test_cluster");
    tls_ = std::make_unique<GoogleAsyncClientThreadLocal>(*api_);
    grpc_client_ =
        std::make_unique<GoogleAsyncClientImpl>(dispatcher_, *tls_, stub_factory_, scope_, config);
  }

  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherImpl dispatcher_;
  Stats::IsolatedStoreImpl* stats_store_; // Ownership transerred to scope_.
  Api::ApiPtr api_;
  Stats::ScopeSharedPtr scope_;
  std::unique_ptr<GoogleAsyncClientThreadLocal> tls_;
  MockStubFactory stub_factory_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  std::unique_ptr<GoogleAsyncClientImpl> grpc_client_;
};

// Validate that a failure in gRPC stub call creation returns immediately with
// status UNAVAILABLE.
TEST_F(EnvoyGoogleAsyncClientImplTest, StreamHttpStartFail) {
  EXPECT_CALL(*stub_factory_.stub_, PrepareCall_(_, _, _)).WillOnce(Return(nullptr));
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(grpc_callbacks, onReceiveTrailingMetadata_(_));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::GrpcStatus::Unavailable, ""));
  auto* grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure in gRPC stub call creation returns immediately with
// status UNAVAILABLE.
TEST_F(EnvoyGoogleAsyncClientImplTest, RequestHttpStartFail) {
  EXPECT_CALL(*stub_factory_.stub_, PrepareCall_(_, _, _)).WillOnce(Return(nullptr));
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::GrpcStatus::Unavailable, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, "test_cluster"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "14"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, injectContext(_));

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, absl::optional<std::chrono::milliseconds>());
  EXPECT_EQ(grpc_request, nullptr);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
