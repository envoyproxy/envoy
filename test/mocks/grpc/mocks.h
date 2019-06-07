#pragma once

#include <cstdint>
#include <string>

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
  ~MockAsyncRequest();

  MOCK_METHOD0(cancel, void());
};

class MockAsyncStream : public RawAsyncStream {
public:
  MockAsyncStream();
  ~MockAsyncStream();

  void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) {
    sendMessageRaw_(request, end_stream);
  }
  MOCK_METHOD2_T(sendMessageRaw_, void(Buffer::InstancePtr& request, bool end_stream));
  MOCK_METHOD0_T(closeStream, void());
  MOCK_METHOD0_T(resetStream, void());
};

template <class ResponseType>
class MockAsyncRequestCallbacks : public AsyncRequestCallbacks<ResponseType> {
public:
  void onSuccess(std::unique_ptr<ResponseType>&& response, Tracing::Span& span) {
    onSuccess_(*response, span);
  }

  MOCK_METHOD1_T(onCreateInitialMetadata, void(Http::HeaderMap& metadata));
  MOCK_METHOD2_T(onSuccess_, void(const ResponseType& response, Tracing::Span& span));
  MOCK_METHOD3_T(onFailure,
                 void(Status::GrpcStatus status, const std::string& message, Tracing::Span& span));
};

template <class ResponseType>
class MockAsyncStreamCallbacks : public AsyncStreamCallbacks<ResponseType> {
public:
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
    onReceiveInitialMetadata_(*metadata);
  }

  void onReceiveMessage(std::unique_ptr<ResponseType>&& message) { onReceiveMessage_(*message); }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
    onReceiveTrailingMetadata_(*metadata);
  }

  MOCK_METHOD1_T(onCreateInitialMetadata, void(Http::HeaderMap& metadata));
  MOCK_METHOD1_T(onReceiveInitialMetadata_, void(const Http::HeaderMap& metadata));
  MOCK_METHOD1_T(onReceiveMessage_, void(const ResponseType& message));
  MOCK_METHOD1_T(onReceiveTrailingMetadata_, void(const Http::HeaderMap& metadata));
  MOCK_METHOD2_T(onRemoteClose, void(Status::GrpcStatus status, const std::string& message));
};

class MockAsyncClient : public RawAsyncClient {
public:
  MOCK_METHOD6_T(sendRaw,
                 AsyncRequest*(absl::string_view service_full_name, absl::string_view method_name,
                               Buffer::InstancePtr&& request, RawAsyncRequestCallbacks& callbacks,
                               Tracing::Span& parent_span,
                               const absl::optional<std::chrono::milliseconds>& timeout));
  MOCK_METHOD3_T(startRaw,
                 RawAsyncStream*(absl::string_view service_full_name, absl::string_view method_name,
                                 RawAsyncStreamCallbacks& callbacks));
};

class MockAsyncClientFactory : public AsyncClientFactory {
public:
  MockAsyncClientFactory();
  ~MockAsyncClientFactory();

  MOCK_METHOD0(create, RawAsyncClientPtr());
};

class MockAsyncClientManager : public AsyncClientManager {
public:
  MockAsyncClientManager();
  ~MockAsyncClientManager();

  MOCK_METHOD3(factoryForGrpcService,
               AsyncClientFactoryPtr(const envoy::api::v2::core::GrpcService& grpc_service,
                                     Stats::Scope& scope, bool skip_cluster_check));
};

MATCHER_P(ProtoBufferEq, expected, "") {
  typename std::remove_const<decltype(expected)>::type proto;
  if (!proto.ParseFromArray(static_cast<char*>(arg->linearize(arg->length())), arg->length())) {
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
  auto equal = ::Envoy::TestUtility::protoEqualIgnoringField(proto, expected, ignored_field);
  if (!equal) {
    std::string but_ignoring = absl::StrCat("(but ignoring ", ignored_field, ")");
    *result_listener << "\n"
                     << "=======================Expected proto:===========================\n"
                     << expected.DebugString() << " " << but_ignoring
                     << "------------------is not equal to actual proto:------------------\n"
                     << proto.DebugString() << " " << but_ignoring
                     << "=================================================================\n";
  }
  return equal;
}

} // namespace Grpc
} // namespace Envoy
