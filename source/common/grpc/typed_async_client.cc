#include "common/grpc/typed_async_client.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Grpc {
namespace Internal {

void sendMessageUntyped(RawAsyncStream* stream, const Protobuf::Message& request, bool end_stream) {
  stream->sendMessageRaw(Common::serializeMessage(request), end_stream);
}

ProtobufTypes::MessagePtr parseMessageUntyped(ProtobufTypes::MessagePtr&& message,
                                              Buffer::InstancePtr&& response) {
  // TODO(htuch): Need to add support for compressed responses as well here.
  if (response->length() > 0) {
    Buffer::ZeroCopyInputStreamImpl stream(std::move(response));
    if (!message->ParseFromZeroCopyStream(&stream)) {
      return nullptr;
    }
  }
  return std::move(message);
}

RawAsyncStream* startUntyped(RawAsyncClient* client,
                             const Protobuf::MethodDescriptor& service_method,
                             RawAsyncStreamCallbacks& callbacks,
                             const Http::AsyncClient::StreamOptions& options) {
  return client->startRaw(service_method.service()->full_name(), service_method.name(), callbacks,
                          options);
}

AsyncRequest* sendUntyped(RawAsyncClient* client, const Protobuf::MethodDescriptor& service_method,
                          const Protobuf::Message& request, RawAsyncRequestCallbacks& callbacks,
                          Tracing::Span& parent_span,
                          const Http::AsyncClient::RequestOptions& options) {
  return client->sendRaw(service_method.service()->full_name(), service_method.name(),
                         Common::serializeMessage(request), callbacks, parent_span, options);
}

} // namespace Internal
} // namespace Grpc
} // namespace Envoy
