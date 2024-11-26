#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {
namespace {

class MockRequestCallbacks : public RequestCallbacks {
public:
  void complete(LimitStatus status, DescriptorStatusListPtr&& descriptor_statuses,
                Http::ResponseHeaderMapPtr&& response_headers_to_add,
                Http::RequestHeaderMapPtr&& request_headers_to_add,
                const std::string& response_body, DynamicMetadataPtr&& dynamic_metadata) override {
    complete_(status, descriptor_statuses.get(), response_headers_to_add.get(),
              request_headers_to_add.get(), response_body, dynamic_metadata.get());
  }

  MOCK_METHOD(void, complete_,
              (LimitStatus status, const DescriptorStatusList* descriptor_statuses,
               const Http::ResponseHeaderMap* response_headers_to_add,
               const Http::RequestHeaderMap* request_headers_to_add,
               const std::string& response_body, const ProtobufWkt::Struct* dynamic_metadata));
};

class RateLimitGrpcClientTest : public testing::Test {
public:
  RateLimitGrpcClientTest()
      : async_client_(new Grpc::MockAsyncClient()),
        client_(Grpc::RawAsyncClientPtr{async_client_},
                absl::optional<std::chrono::milliseconds>()) {}

  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImpl client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;
  StreamInfo::MockStreamInfo stream_info_;
};

TEST_F(RateLimitGrpcClientTest, Basic) {
  std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse> response;

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    Http::TestRequestHeaderMapImpl headers;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar", 0}}}}, 0);
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(client_), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
              std::string service_name = "envoy.service.ratelimit.v3.RateLimitService";
              EXPECT_EQ(service_name, service_full_name);
              EXPECT_EQ("ShouldRateLimit", method_name);
              return &async_request_;
            }));

    client_.limit(request_callbacks_, "foo", {{{{"foo", "bar", 0}}}}, Tracing::NullSpan::instance(),
                  stream_info_);

    client_.onCreateInitialMetadata(headers);
    EXPECT_EQ(nullptr, headers.RequestId());

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT);
    EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("over_limit")));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OverLimit, _, _, _, _, _));
    client_.onSuccess(std::move(response), span_);
  }

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    Http::TestRequestHeaderMapImpl headers;
    GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar", 0}, {"bar", "baz", 0}}}}, 0);
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.limit(request_callbacks_, "foo", {{{{"foo", "bar", 0}, {"bar", "baz", 0}}}},
                  Tracing::NullSpan::instance(), stream_info_);

    client_.onCreateInitialMetadata(headers);

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OK);
    EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("ok")));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OK, _, _, _, _, _));
    client_.onSuccess(std::move(response), span_);
  }

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    GrpcClientImpl::createRequest(
        request, "foo",
        {{{{"foo", "bar", 0}, {"bar", "baz", 0}}}, {{{"foo2", "bar2", 0}, {"bar2", "baz2", 0}}}},
        0);
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.limit(
        request_callbacks_, "foo",
        {{{{"foo", "bar", 0}, {"bar", "baz", 0}}}, {{{"foo2", "bar2", 0}, {"bar2", "baz2", 0}}}},
        Tracing::NullSpan::instance(), stream_info_);

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::Error, _, _, _, _, _));
    client_.onFailure(Grpc::Status::Unknown, "", span_);
  }

  {
    envoy::service::ratelimit::v3::RateLimitRequest request;
    Http::TestRequestHeaderMapImpl headers;
    GrpcClientImpl::createRequest(
        request, "foo",
        {{{{"foo", "bar", 0}, {"bar", "baz", 0}}, {{42, envoy::type::v3::RateLimitUnit::MINUTE}}}},
        0);
    EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), _, _, _))
        .WillOnce(Return(&async_request_));

    client_.limit(
        request_callbacks_, "foo",
        {{{{"foo", "bar", 0}, {"bar", "baz", 0}}, {{42, envoy::type::v3::RateLimitUnit::MINUTE}}}},
        Tracing::NullSpan::instance(), stream_info_);

    client_.onCreateInitialMetadata(headers);

    response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
    response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OK);
    EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("ok")));
    EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OK, _, _, _, _, _));
    client_.onSuccess(std::move(response), span_);
  }
}

TEST_F(RateLimitGrpcClientTest, Cancel) {
  std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse> response;

  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request_));

  client_.limit(request_callbacks_, "foo", {{{{"foo", "bar", 0}}}}, Tracing::NullSpan::instance(),
                stream_info_);

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

// Makes request with hits_addend > 0.
TEST_F(RateLimitGrpcClientTest, RequestWithHitsAddend) {
  std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse> response;
  envoy::service::ratelimit::v3::RateLimitRequest request;
  Http::TestRequestHeaderMapImpl headers;
  uint32_t hits_addend = 5;
  GrpcClientImpl::createRequest(request, "foo", {{{{"foo", "bar", 0}}}}, hits_addend);
  EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(client_), _, _))
      .WillOnce(
          Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                        Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                        const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
            std::string service_name = "envoy.service.ratelimit.v3.RateLimitService";
            EXPECT_EQ(service_name, service_full_name);
            EXPECT_EQ("ShouldRateLimit", method_name);
            return &async_request_;
          }));

  client_.limit(request_callbacks_, "foo", {{{{"foo", "bar", 0}}}}, Tracing::NullSpan::instance(),
                stream_info_, hits_addend);

  client_.onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());

  response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
  response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT);
  EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("over_limit")));
  EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OverLimit, _, _, _, _, _));
  client_.onSuccess(std::move(response), span_);
}

// Now add these test cases:
TEST_F(RateLimitGrpcClientTest, CreateRequestWithEntryLevelHitsAddend) {
  envoy::service::ratelimit::v3::RateLimitRequest request;
  const uint32_t global_hits = 1;

  std::vector<Envoy::RateLimit::Descriptor> descriptors = {{{
      {"key1", "value1", 5}, // Entry with hits_addend override
      {"key2", "value2", 0}  // Entry without override
  }}};

  GrpcClientImpl::createRequest(request, "test_domain", descriptors, global_hits);

  ASSERT_EQ(request.domain(), "test_domain");
  ASSERT_EQ(request.hits_addend(), global_hits);
  ASSERT_EQ(request.descriptors_size(), 1);

  const auto& descriptor = request.descriptors(0);
  ASSERT_EQ(descriptor.entries_size(), 2);

  // First entry should have hits_addend override
  EXPECT_EQ(descriptor.entries(0).key(), "key1");
  EXPECT_EQ(descriptor.entries(0).value(), "value1");
  EXPECT_EQ(descriptor.entries(0).hits_addend(), 5);

  // Second entry should have default hits_addend
  EXPECT_EQ(descriptor.entries(1).key(), "key2");
  EXPECT_EQ(descriptor.entries(1).value(), "value2");
  EXPECT_EQ(descriptor.entries(1).hits_addend(), 0);
}

TEST_F(RateLimitGrpcClientTest, CreateRequestWithMultipleDescriptors) {
  envoy::service::ratelimit::v3::RateLimitRequest request;
  const uint32_t global_hits = 2;

  std::vector<Envoy::RateLimit::Descriptor> descriptors = {
      {{
          {"key1", "value1", 3} // First descriptor with override
      }},
      {{
          {"key2", "value2", 0}, // Second descriptor with explicit zero
          {"key3", "value3", 0}  // Entry without override
      }}};

  GrpcClientImpl::createRequest(request, "test_domain", descriptors, global_hits);

  ASSERT_EQ(request.descriptors_size(), 2);

  // Check first descriptor
  const auto& descriptor1 = request.descriptors(0);
  ASSERT_EQ(descriptor1.entries_size(), 1);
  EXPECT_EQ(descriptor1.entries(0).key(), "key1");
  EXPECT_EQ(descriptor1.entries(0).value(), "value1");
  EXPECT_EQ(descriptor1.entries(0).hits_addend(), 3);

  // Check second descriptor
  const auto& descriptor2 = request.descriptors(1);
  ASSERT_EQ(descriptor2.entries_size(), 2);
  EXPECT_EQ(descriptor2.entries(0).key(), "key2");
  EXPECT_EQ(descriptor2.entries(0).value(), "value2");
  EXPECT_EQ(descriptor2.entries(0).hits_addend(), 0);
  EXPECT_EQ(descriptor2.entries(1).key(), "key3");
  EXPECT_EQ(descriptor2.entries(1).value(), "value3");
  EXPECT_EQ(descriptor2.entries(1).hits_addend(), 0);
}

TEST_F(RateLimitGrpcClientTest, FullRequestResponseCycleWithHitsAddend) {
  envoy::service::ratelimit::v3::RateLimitRequest request;
  Http::TestRequestHeaderMapImpl headers;

  std::vector<Envoy::RateLimit::Descriptor> descriptors = {{{{"key1", "value1", 5}}}};

  const uint32_t global_hits = 1;
  GrpcClientImpl::createRequest(request, "test_domain", descriptors, global_hits);

  EXPECT_CALL(*async_client_, sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(client_), _, _))
      .WillOnce(Return(&async_request_));

  client_.limit(request_callbacks_, "test_domain", descriptors, Tracing::NullSpan::instance(),
                stream_info_, global_hits);

  auto response = std::make_unique<envoy::service::ratelimit::v3::RateLimitResponse>();
  response->set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OK);
  EXPECT_CALL(span_, setTag(Eq("ratelimit_status"), Eq("ok")));
  EXPECT_CALL(request_callbacks_, complete_(LimitStatus::OK, _, _, _, _, _));
  client_.onSuccess(std::move(response), span_);
}

} // namespace
} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
