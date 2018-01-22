#pragma once

#include <cstdint>
#include <string>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"

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
  MOCK_METHOD2_T(sendMessage, void(const Protobuf::Message& request, bool end_stream));
  MOCK_METHOD0_T(closeStream, void());
  MOCK_METHOD0_T(resetStream, void());
};

template <class ResponseType>
class MockAsyncRequestCallbacks : public TypedAsyncRequestCallbacks<ResponseType> {
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
class MockAsyncStreamCallbacks : public TypedAsyncStreamCallbacks<ResponseType> {
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

class MockAsyncClient : public AsyncClient {
public:
  MOCK_METHOD5_T(send, AsyncRequest*(const Protobuf::MethodDescriptor& service_method,
                                     const Protobuf::Message& request,
                                     AsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                     const Optional<std::chrono::milliseconds>& timeout));
  MOCK_METHOD2_T(start, AsyncStream*(const Protobuf::MethodDescriptor& service_method,
                                     AsyncStreamCallbacks& callbacks));
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

  MOCK_METHOD2(factoryForGrpcService,
               AsyncClientFactoryPtr(const envoy::api::v2::GrpcService& grpc_service,
                                     Stats::Scope& scope));
};

} // namespace Grpc
} // namespace Envoy
