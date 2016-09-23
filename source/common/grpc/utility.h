#pragma once

#include "common/http/message_impl.h"

#include "google/protobuf/message.h"

namespace Grpc {

class Utility {
public:
  static Buffer::InstancePtr serializeBody(const proto::Message& message);
  static Http::MessagePtr prepareHeaders(const proto::MethodDescriptor* method);
};

} // Grpc
