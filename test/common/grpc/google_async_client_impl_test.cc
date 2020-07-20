#include "envoy/config/core/v3/grpc_service.pb.h"
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
using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class MockGenericStub : public GoogleStub {
public:
  MOCK_METHOD(grpc::GenericClientAsyncReaderWriter*, PrepareCall_,
              (grpc::ClientContext * context, const grpc::string& method,
               grpc::CompletionQueue* cq));

  std::unique_ptr<grpc::GenericClientAsyncReaderWriter>
  PrepareCall(grpc::ClientContext* context, const grpc::string& method,
              grpc::CompletionQueue* cq) override {
    return std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(PrepareCall_(context, method, cq));
  }
};

class MockStubFactory : public GoogleStubFactory {
public:
  GoogleStubSharedPtr createStub(std::shared_ptr<grpc::Channel> /*channel*/) override {
    return shared_stub_;
  }

  MockGenericStub* stub_ = new MockGenericStub();
  GoogleStubSharedPtr shared_stub_{stub_};
};

class EnvoyGoogleAsyncClientImplTest : public testing::Test {
public:
  EnvoyGoogleAsyncClientImplTest()
      : stats_store_(new Stats::IsolatedStoreImpl), api_(Api::createApiForTest(*stats_store_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), scope_(stats_store_),
        method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")),
        stat_names_(scope_->symbolTable()) {

    auto* google_grpc = config_.mutable_google_grpc();
    google_grpc->set_target_uri("fake_address");
    google_grpc->set_stat_prefix("test_cluster");
    tls_ = std::make_unique<GoogleAsyncClientThreadLocal>(*api_);
  }

  virtual void initialize() {
    grpc_client_ = std::make_unique<GoogleAsyncClientImpl>(*dispatcher_, *tls_, stub_factory_,
                                                           scope_, config_, *api_, stat_names_);
  }

  envoy::config::core::v3::GrpcService config_;
  DangerousDeprecatedTestTime test_time_;
  Stats::IsolatedStoreImpl* stats_store_; // Ownership transferred to scope_.
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::ScopeSharedPtr scope_;
  GoogleAsyncClientThreadLocalPtr tls_;
  MockStubFactory stub_factory_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  StatNames stat_names_;
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> grpc_client_;
};

// Validate that a failure in gRPC stub call creation returns immediately with
// status UNAVAILABLE.
TEST_F(EnvoyGoogleAsyncClientImplTest, StreamHttpStartFail) {
  initialize();

  EXPECT_CALL(*stub_factory_.stub_, PrepareCall_(_, _, _)).WillOnce(Return(nullptr));
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(grpc_callbacks, onReceiveTrailingMetadata_(_));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, ""));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_TRUE(grpc_stream == nullptr);
}

// Validate that a failure in gRPC stub call creation returns immediately with
// status UNAVAILABLE.
TEST_F(EnvoyGoogleAsyncClientImplTest, RequestHttpStartFail) {
  initialize();

  EXPECT_CALL(*stub_factory_.stub_, PrepareCall_(_, _, _)).WillOnce(Return(nullptr));
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::WellKnownGrpcStatus::Unavailable, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("14")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, injectContext(_));

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Http::AsyncClient::RequestOptions());
  EXPECT_TRUE(grpc_request == nullptr);
}

class EnvoyGoogleLessMockedAsyncClientImplTest : public EnvoyGoogleAsyncClientImplTest {
public:
  void initialize() override {
    grpc_client_ = std::make_unique<GoogleAsyncClientImpl>(*dispatcher_, *tls_, real_stub_factory_,
                                                           scope_, config_, *api_, stat_names_);
  }

  GoogleGenericStubFactory real_stub_factory_;
};

TEST_F(EnvoyGoogleLessMockedAsyncClientImplTest, TestOverflow) {
  // Set an (unreasonably) low byte limit.
  auto* google_grpc = config_.mutable_google_grpc();
  google_grpc->mutable_per_stream_buffer_limit_bytes()->set_value(1);
  initialize();

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  AsyncStream<helloworld::HelloRequest> grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::RequestOptions());
  EXPECT_FALSE(grpc_stream == nullptr);
  EXPECT_FALSE(grpc_stream->isAboveWriteBufferHighWatermark());

  // With no data in the message, it won't back up.
  helloworld::HelloRequest request_msg;
  grpc_stream->sendMessage(request_msg, false);
  EXPECT_FALSE(grpc_stream->isAboveWriteBufferHighWatermark());

  // With actual data we pass the very small byte limit.
  request_msg.set_name("bob");
  grpc_stream->sendMessage(request_msg, false);
  EXPECT_TRUE(grpc_stream->isAboveWriteBufferHighWatermark());
}

} // namespace
} // namespace Grpc
} // namespace Envoy
