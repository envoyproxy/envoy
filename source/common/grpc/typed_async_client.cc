#include "common/grpc/typed_async_client.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Grpc {

void UntypedAsyncStream::sendMessageUntyped(const Protobuf::Message& request, bool end_stream) {
  stream_->sendMessage(stream_->isGrpcHeaderRequired() ? Common::serializeBody(request)
                                                       : Common::serializeMessage(request),
                       end_stream);
}

void UntypedAsyncRequestCallbacks::onSuccess(Buffer::InstancePtr&& response, Tracing::Span& span) {
  ProtobufTypes::MessagePtr response_message = createEmptyResponse();
  // TODO(htuch): Need to add support for compressed responses as well here.
  if (response->length() > 0) {
    Buffer::ZeroCopyInputStreamImpl stream(std::move(response));
    if (!response_message->ParseFromZeroCopyStream(&stream)) {
      onFailure(Status::GrpcStatus::Internal, "", span);
      return;
    }
  }
  onSuccessUntyped(std::move(response_message), span);
}

bool UntypedAsyncStreamCallbacks::onReceiveMessage(Buffer::InstancePtr&& response) {
  ProtobufTypes::MessagePtr response_message = createEmptyResponse();
  // TODO(htuch): Need to add support for compressed responses as well here.
  if (response->length() > 0) {
    Buffer::ZeroCopyInputStreamImpl stream(std::move(response));
    if (!response_message->ParseFromZeroCopyStream(&stream)) {
      return false;
    }
  }
  onReceiveMessageUntyped(std::move(response_message));
  return true;
}

UntypedAsyncStream
UntypedAsyncClient::startUntyped(const Protobuf::MethodDescriptor& service_method,
                                 UntypedAsyncStreamCallbacks& callbacks) {
  return UntypedAsyncStream(
      client_->start(service_method.service()->full_name(), service_method.name(), callbacks),
      client_->isGrpcHeaderRequired());
}

AsyncRequest*
UntypedAsyncClient::sendUntyped(const Protobuf::MethodDescriptor& service_method,
                                const Protobuf::Message& request,
                                UntypedAsyncRequestCallbacks& callbacks, Tracing::Span& parent_span,
                                const absl::optional<std::chrono::milliseconds>& timeout) {
  return client_->send(service_method.service()->full_name(), service_method.name(),
                       client_->isGrpcHeaderRequired() ? Common::serializeBody(request)
                                                       : Common::serializeMessage(request),
                       callbacks, parent_span, timeout);
}

} // namespace Grpc
} // namespace Envoy
