#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/stats/scope.h"

#include "common/grpc/typed_async_client.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Grpc {

class MockAsyncRequest : public AsyncRequest {
public:
  MockAsyncRequest();
  ~MockAsyncRequest() override;

  MOCK_METHOD(void, cancel, ());
};

class MockAsyncStream : public RawAsyncStream {
public:
  MockAsyncStream();
  ~MockAsyncStream() override;

  void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) override {
    sendMessageRaw_(request, end_stream);
  }
  MOCK_METHOD(void, sendMessageRaw_, (Buffer::InstancePtr & request, bool end_stream));
  MOCK_METHOD(void, closeStream, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(bool, isAboveWriteBufferHighWatermark, (), (const));
};

template <class ResponseType> using ResponseTypePtr = std::unique_ptr<ResponseType>;

template <class ResponseType>
class MockAsyncRequestCallbacks : public AsyncRequestCallbacks<ResponseType> {
public:
  void onSuccess(ResponseTypePtr<ResponseType>&& response, Tracing::Span& span) {
    onSuccess_(*response, span);
  }

  MOCK_METHOD(void, onCreateInitialMetadata, (Http::RequestHeaderMap & metadata));
  MOCK_METHOD(void, onSuccess_, (const ResponseType& response, Tracing::Span& span));
  MOCK_METHOD(void, onFailure,
              (Status::GrpcStatus status, const std::string& message, Tracing::Span& span));
};

template <class ResponseType>
class MockAsyncStreamCallbacks : public AsyncStreamCallbacks<ResponseType> {
public:
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) {
    onReceiveInitialMetadata_(*metadata);
  }

  void onReceiveMessage(ResponseTypePtr<ResponseType>&& message) { onReceiveMessage_(*message); }

  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) {
    onReceiveTrailingMetadata_(*metadata);
  }

  MOCK_METHOD(void, onCreateInitialMetadata, (Http::RequestHeaderMap & metadata));
  MOCK_METHOD(void, onReceiveInitialMetadata_, (const Http::ResponseHeaderMap& metadata));
  MOCK_METHOD(void, onReceiveMessage_, (const ResponseType& message));
  MOCK_METHOD(void, onReceiveTrailingMetadata_, (const Http::ResponseTrailerMap& metadata));
  MOCK_METHOD(void, onRemoteClose, (Status::GrpcStatus status, const std::string& message));
};

class MockAsyncClient : public RawAsyncClient {
public:
  MockAsyncClient();
  ~MockAsyncClient() override;

  MOCK_METHOD(AsyncRequest*, sendRaw,
              (absl::string_view service_full_name, absl::string_view method_name,
               Buffer::InstancePtr&& request, RawAsyncRequestCallbacks& callbacks,
               Tracing::Span& parent_span, const Http::AsyncClient::RequestOptions& options));
  MOCK_METHOD(RawAsyncStream*, startRaw,
              (absl::string_view service_full_name, absl::string_view method_name,
               RawAsyncStreamCallbacks& callbacks,
               const Http::AsyncClient::StreamOptions& options));

  std::unique_ptr<testing::NiceMock<Grpc::MockAsyncRequest>> async_request_;
};

class MockAsyncClientFactory : public AsyncClientFactory {
public:
  MockAsyncClientFactory();
  ~MockAsyncClientFactory() override;

  MOCK_METHOD(RawAsyncClientPtr, create, ());
};

class MockAsyncClientManager : public AsyncClientManager {
public:
  MockAsyncClientManager();
  ~MockAsyncClientManager() override;

  MOCK_METHOD(AsyncClientFactoryPtr, factoryForGrpcService,
              (const envoy::config::core::v3::GrpcService& grpc_service, Stats::Scope& scope,
               bool skip_cluster_check));
};

MATCHER_P(ProtoBufferEq, expected, "") {
  typename std::remove_const<decltype(expected)>::type proto;
  if (!proto.ParseFromString(arg->toString())) {
    *result_listener << "\nParse of buffer failed\n";
    return false;
  }
  auto equal = ::Envoy::TestUtility::protoEqual(proto, expected);
  if (!equal) {
    *result_listener << "\n"
                     << "=======================Expected proto:===========================\n"
                     << expected.DebugString()
                     << "------------------is not equal to actual proto:------------------\n"
                     << proto.DebugString()
                     << "=================================================================\n";
  }
  return equal;
}

MATCHER_P2(ProtoBufferEqIgnoringField, expected, ignored_field, "") {
  typename std::remove_const<decltype(expected)>::type proto;
  if (!proto.ParseFromArray(static_cast<char*>(arg->linearize(arg->length())), arg->length())) {
    *result_listener << "\nParse of buffer failed\n";
    return false;
  }
  const bool equal = ::Envoy::TestUtility::protoEqualIgnoringField(proto, expected, ignored_field);
  if (!equal) {
    std::string but_ignoring = absl::StrCat("(but ignoring ", ignored_field, ")");
    *result_listener << "\n"
                     << ::Envoy::TestUtility::addLeftAndRightPadding("Expected proto:") << "\n"
                     << ::Envoy::TestUtility::addLeftAndRightPadding(but_ignoring) << "\n"
                     << expected.DebugString()
                     << ::Envoy::TestUtility::addLeftAndRightPadding(
                            "is not equal to actual proto:")
                     << "\n"
                     << proto.DebugString()
                     << ::Envoy::TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(ProtoBufferEqIgnoreRepeatedFieldOrdering, expected, "") {
  typename std::remove_const<decltype(expected)>::type proto;
  if (!proto.ParseFromArray(static_cast<char*>(arg->linearize(arg->length())), arg->length())) {
    *result_listener << "\nParse of buffer failed\n";
    return false;
  }
  const bool equal =
      ::Envoy::TestUtility::protoEqual(proto, expected, /*ignore_repeated_field_ordering=*/true);
  if (!equal) {
    *result_listener << "\n"
                     << "=======================Expected proto:===========================\n"
                     << expected.DebugString()
                     << "------------------is not equal to actual proto:------------------\n"
                     << proto.DebugString()
                     << "=================================================================\n";
  }
  return equal;
}

} // namespace Grpc
} // namespace Envoy
