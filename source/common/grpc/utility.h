#pragma once

#include "common/http/message_impl.h"

#include "google/protobuf/message.h"

namespace Grpc {

class Utility {
public:
  static Buffer::InstancePtr serializeBody(const google::protobuf::Message& message);
  static Http::MessagePtr prepareHeaders(const google::protobuf::MethodDescriptor& method,
                                         const std::string& upstream_cluster);
};

} // Grpc
