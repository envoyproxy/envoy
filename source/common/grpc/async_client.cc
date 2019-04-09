#include "envoy/grpc/async_client.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Grpc {

void AsyncStream::sendMessage(const Protobuf::Message& request, bool end_stream) {
  sendRawMessage(Common::serializeBody(request), end_stream);
}

AsyncRequest* AsyncClient::send(const Protobuf::MethodDescriptor& service_method,
                                const Protobuf::Message& request, AsyncRequestCallbacks& callbacks,
                                Tracing::Span& parent_span,
                                const absl::optional<std::chrono::milliseconds>& timeout) {
  return sendRaw(service_method.service()->full_name(), service_method.name(),
                 Common::serializeBody(request), callbacks, parent_span, timeout);
}

AsyncStream* AsyncClient::start(const Protobuf::MethodDescriptor& service_method,
                                AsyncStreamCallbacks& callbacks) {
  return startRaw(service_method.service()->full_name(), service_method.name(), callbacks);
}

void AsyncRequestCallbacks::onSuccessRaw(Buffer::InstancePtr response, Tracing::Span& span) {
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

bool AsyncStreamCallbacks::onReceiveRawMessage(Buffer::InstancePtr response) {
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

} // namespace Grpc
} // namespace Envoy
