#include "common.h"
#include "utility.h"

#include "common/http/headers.h"

namespace Grpc {

Buffer::InstancePtr Utility::serializeBody(const google::protobuf::Message& message) {
  // http://www.grpc.io/docs/guides/wire.html
  Buffer::InstancePtr body(new Buffer::OwnedImpl());
  uint8_t compressed = 0;
  body->add(&compressed, sizeof(compressed));
  uint32_t size = htonl(message.ByteSize());
  body->add(&size, sizeof(size));
  body->add(message.SerializeAsString());

  return body;
}

Http::MessagePtr Utility::prepareHeaders(
  const std::string& upstream_cluster, const std::string& upstream_cluster, const std::string& service_full_name, const std::string& method_name) {
  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().addViaMoveValue(Http::Headers::get().Scheme, "http");
  message->headers().addViaMoveValue(Http::Headers::get().Method, "POST");
  message->headers().addViaMoveValue(
      Http::Headers::get().Path,
      fmt::format("/{}/{}", service_full_name, method_name));
  message->headers().addViaCopy(Http::Headers::get().Host, upstream_cluster);
  message->headers().addViaCopy(Http::Headers::get().ContentType, Common::GRPC_CONTENT_TYPE);

  return message;
}

} // Grpc