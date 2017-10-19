#pragma once

#include <cstdint>
#include <string>

#include "envoy/grpc/async_client.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Grpc {

class MockAsyncRequest : public AsyncRequest {
public:
  MockAsyncRequest();
  ~MockAsyncRequest();

  MOCK_METHOD0(cancel, void());
};

template <class RequestType> class MockAsyncStream : public AsyncStream<RequestType> {
public:
  MOCK_METHOD2_T(sendMessage, void(const RequestType& request, bool end_stream));
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

template <class RequestType, class ResponseType>
class MockAsyncClient : public AsyncClient<RequestType, ResponseType> {
public:
  MOCK_METHOD5_T(send, AsyncRequest*(const Protobuf::MethodDescriptor& service_method,
                                     const RequestType& request,
                                     AsyncRequestCallbacks<ResponseType>& callbacks,
                                     Tracing::Span& parent_span,
                                     const Optional<std::chrono::milliseconds>& timeout));
  MOCK_METHOD2_T(start, AsyncStream<RequestType>*(const Protobuf::MethodDescriptor& service_method,
                                                  AsyncStreamCallbacks<ResponseType>& callbacks));
};

} // namespace Grpc
} // namespace Envoy
