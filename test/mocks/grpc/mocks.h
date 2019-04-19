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

class MockAsyncStream : public AsyncStream {
public:
  MockAsyncStream();
  ~MockAsyncStream();

  MOCK_METHOD2_T(sendMessage, void(Buffer::InstancePtr&& request, bool end_stream));
  MOCK_METHOD0_T(closeStream, void());
  MOCK_METHOD0_T(resetStream, void());
  MOCK_METHOD0_T(isGrpcHeaderRequired, bool());
};

template <class ResponseType>
class MockAsyncRequestCallbacks : public TypedAsyncRequestCallbacks<ResponseType> {
public:
  void onSuccessTyped(std::unique_ptr<ResponseType>&& response, Tracing::Span& span) {
    onSuccessTyped_(*response, span);
  }

  MOCK_METHOD1_T(onCreateInitialMetadata, void(Http::HeaderMap& metadata));
  MOCK_METHOD2_T(onSuccessTyped_, void(const ResponseType& response, Tracing::Span& span));
  MOCK_METHOD3_T(onFailure,
                 void(Status::GrpcStatus status, const std::string& message, Tracing::Span& span));
};

template <class ResponseType>
class MockAsyncStreamCallbacks : public TypedAsyncStreamCallbacks<ResponseType> {
public:
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
    onReceiveInitialMetadata_(*metadata);
  }

  void onReceiveMessageTyped(std::unique_ptr<ResponseType>&& message) {
    onReceiveMessageTyped_(*message);
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
    onReceiveTrailingMetadata_(*metadata);
  }

  MOCK_METHOD1_T(onCreateInitialMetadata, void(Http::HeaderMap& metadata));
  MOCK_METHOD1_T(onReceiveInitialMetadata_, void(const Http::HeaderMap& metadata));
  MOCK_METHOD1_T(onReceiveMessageTyped_, void(const ResponseType& message));
  MOCK_METHOD1_T(onReceiveTrailingMetadata_, void(const Http::HeaderMap& metadata));
  MOCK_METHOD2_T(onRemoteClose, void(Status::GrpcStatus status, const std::string& message));
};

class MockAsyncClient : public AsyncClient {
public:
  MOCK_METHOD6_T(send, AsyncRequest*(absl::string_view service_full_name,
                                     absl::string_view method_name, Buffer::InstancePtr&& request,
                                     AsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                     const absl::optional<std::chrono::milliseconds>& timeout));
  MOCK_METHOD3_T(start,
                 AsyncStream*(absl::string_view service_full_name, absl::string_view method_name,
                              AsyncStreamCallbacks& callbacks));
  MOCK_METHOD0_T(isGrpcHeaderRequired, bool());
};

class MockAsyncClientFactory : public AsyncClientFactory {
public:
  MockAsyncClientFactory();
  ~MockAsyncClientFactory();

  MOCK_METHOD0(create, AsyncClientPtr());
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
  proto.ParseFromArray(static_cast<char*>(arg->linearize(arg->length())), arg->length());
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

} // namespace Grpc
} // namespace Envoy
