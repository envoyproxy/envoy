#pragma once

#include "common/http/message_impl.h"

#include "google/protobuf/message.h"

namespace Grpc {

class Utility {
public:
  static Buffer::InstancePtr serializeBody(const google::protobuf::Message& message);
  static Http::MessagePtr prepareHeaders(const std::string& upstream_cluster,
                                         const std::string& service_full_name,
                                         const std::string& method_name);
};

} // Grpc
