#include "common/common/empty_string.h"
#include "common/ratelimit/ratelimit_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::WithArg;

namespace RateLimit {

class MockRequestCallbacks : public RequestCallbacks {
public:
  MOCK_METHOD1(complete, void(LimitStatus status));
};

class RateLimitGrpcClientTest : public testing::Test, public Grpc::RpcChannelFactory {
public:
  RateLimitGrpcClientTest()
      : channel_(new Grpc::MockRpcChannel()),
        client_(*this, Optional<std::chrono::milliseconds>()) {}

  // Grpc::RpcChannelFactory
  Grpc::RpcChannelPtr create(Grpc::RpcChannelCallbacks& callbacks,
                             const Optional<std::chrono::milliseconds>&) {
    channel_callbacks_ = &callbacks;
    return Grpc::RpcChannelPtr{channel_};
  }

  Grpc::MockRpcChannel* channel_;
  Grpc::RpcChannelCallbacks* channel_callbacks_;
  GrpcClientImpl client_;
  MockRequestCallbacks request_callbacks_;
};

TEST_F(RateLimitGrpcClientTest, Basic) {
  pb::lyft::ratelimit::RateLimitResponse* response;

  {
    pb::lyft::ratelimit::RateLimitRequest request;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}}}});
    EXPECT_CALL(*channel_, CallMethod(_, _, ProtoMessageEqual(&request), _, nullptr))
        .WillOnce(WithArg<3>(Invoke([&](proto::Message* raw_response) -> void {
          response = dynamic_cast<pb::lyft::ratelimit::RateLimitResponse*>(raw_response);
        })));

    client_.limit(request_callbacks_, "foo", {{{{"foo", "bar"}}}}, EMPTY_STRING);

    response->Clear();
    response->set_overall_code(pb::lyft::ratelimit::RateLimitResponse_Code_OVER_LIMIT);
    EXPECT_CALL(request_callbacks_, complete(LimitStatus::OverLimit));
    channel_callbacks_->onSuccess();
  }

  {
    pb::lyft::ratelimit::RateLimitRequest request;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}}});
    EXPECT_CALL(*channel_, CallMethod(_, _, ProtoMessageEqual(&request), _, nullptr))
        .WillOnce(WithArg<3>(Invoke([&](proto::Message* raw_response) -> void {
          response = dynamic_cast<pb::lyft::ratelimit::RateLimitResponse*>(raw_response);
        })));

    client_.limit(request_callbacks_, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}}}, EMPTY_STRING);

    response->Clear();
    response->set_overall_code(pb::lyft::ratelimit::RateLimitResponse_Code_OK);
    EXPECT_CALL(request_callbacks_, complete(LimitStatus::OK));
    channel_callbacks_->onSuccess();
  }

  {
    pb::lyft::ratelimit::RateLimitRequest request;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar"}, {"bar", "baz"}}},
                                                   {{{"foo2", "bar2"}, {"bar2", "baz2"}}}});
    EXPECT_CALL(*channel_, CallMethod(_, _, ProtoMessageEqual(&request), _, nullptr))
        .WillOnce(WithArg<3>(Invoke([&](proto::Message* raw_response) -> void {
          response = dynamic_cast<pb::lyft::ratelimit::RateLimitResponse*>(raw_response);
        })));

    client_.limit(request_callbacks_, "foo",
                  {{{{"foo", "bar"}, {"bar", "baz"}}}, {{{"foo2", "bar2"}, {"bar2", "baz2"}}}},
                  EMPTY_STRING);

    response->Clear();
    EXPECT_CALL(request_callbacks_, complete(LimitStatus::Error));
    channel_callbacks_->onFailure(Optional<uint64_t>(), "foo");
  }
}

TEST_F(RateLimitGrpcClientTest, Cancel) {
  pb::lyft::ratelimit::RateLimitResponse* response;

  EXPECT_CALL(*channel_, CallMethod(_, _, _, _, nullptr))
      .WillOnce(WithArg<3>(Invoke([&](proto::Message* raw_response) -> void {
        response = dynamic_cast<pb::lyft::ratelimit::RateLimitResponse*>(raw_response);
      })));

  client_.limit(request_callbacks_, "foo", {{{{"foo", "bar"}}}}, EMPTY_STRING);

  EXPECT_CALL(*channel_, cancel());
  client_.cancel();
}

TEST(RateLimitGrpcFactoryTest, NoCluster) {
  std::string json = R"EOF(
  {
    "cluster_name": "foo"
  }
  )EOF";

  Json::StringLoader config(json);
  Upstream::MockClusterManager cm;
  Stats::IsolatedStoreImpl stats_store;

  EXPECT_CALL(cm, get("foo")).WillOnce(Return(nullptr));
  EXPECT_THROW(GrpcFactoryImpl(config, cm, stats_store), EnvoyException);
}

TEST(RateLimitGrpcFactoryTest, Create) {
  std::string json = R"EOF(
  {
    "cluster_name": "foo"
  }
  )EOF";

  Json::StringLoader config(json);
  Upstream::MockClusterManager cm;
  Stats::IsolatedStoreImpl stats_store;

  EXPECT_CALL(cm, get("foo"));
  GrpcFactoryImpl factory(config, cm, stats_store);
  factory.create(Optional<std::chrono::milliseconds>());
}

TEST(RateLimitNullFactoryTest, Basic) {
  NullFactoryImpl factory;
  ClientPtr client = factory.create(Optional<std::chrono::milliseconds>());
  MockRequestCallbacks request_callbacks;
  EXPECT_CALL(request_callbacks, complete(LimitStatus::OK));
  client->limit(request_callbacks, "foo", {{{{"foo", "bar"}}}}, EMPTY_STRING);
  client->cancel();
}

} // RateLimit
