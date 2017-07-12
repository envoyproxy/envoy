#pragma once

#include <cstdint>
#include <string>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/rpc_channel.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Grpc {

template <class RequestType> class MockAsyncClientStream : public AsyncClientStream<RequestType> {
public:
  MOCK_METHOD1_T(sendMessage, void(const RequestType& request));
  MOCK_METHOD0_T(closeStream, void());
  MOCK_METHOD0_T(resetStream, void());
};

template <class ResponseType>
class MockAsyncClientCallbacks : public AsyncClientCallbacks<ResponseType> {
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
  MOCK_METHOD1_T(onRemoteClose, void(Status::GrpcStatus status));
};

template <class RequestType, class ResponseType>
class MockAsyncClient : public AsyncClient<RequestType, ResponseType> {
public:
  MOCK_METHOD3_T(
      start, AsyncClientStream<RequestType>*(const Protobuf::MethodDescriptor& service_method,
                                             AsyncClientCallbacks<ResponseType>& callbacks,
                                             const Optional<std::chrono::milliseconds>& timeout));
};

class MockRpcChannelCallbacks : public RpcChannelCallbacks {
public:
  MockRpcChannelCallbacks();
  ~MockRpcChannelCallbacks();

  MOCK_METHOD1(onPreRequestCustomizeHeaders, void(Http::HeaderMap& headers));
  MOCK_METHOD0(onSuccess, void());
  MOCK_METHOD2(onFailure, void(const Optional<uint64_t>& grpc_status, const std::string& message));
};

class MockRpcChannel : public RpcChannel {
public:
  MockRpcChannel();
  ~MockRpcChannel();

  MOCK_METHOD0(cancel, void());
  MOCK_METHOD5(CallMethod,
               void(const Protobuf::MethodDescriptor* method, Protobuf::RpcController* controller,
                    const Protobuf::Message* request, Protobuf::Message* response,
                    Protobuf::Closure* done));
};

} // namespace Grpc

MATCHER_P(ProtoMessageEqual, rhs, "") {
  return arg->SerializeAsString() == rhs->SerializeAsString();
}
} // namespace Envoy
