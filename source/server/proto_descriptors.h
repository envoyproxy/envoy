#pragma once

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

// This function validates that the method descriptors for gRPC services and type descriptors that
// are referenced in Any messages are available in the descriptor pool.
void validateProtoDescriptors();

// Translates an xDS resource type_url to the name of the delta gRPC service that carries it.
const Protobuf::MethodDescriptor& deltaGrpcMethod(absl::string_view type_url);
// Translates an xDS resource type_url to the name of the state-of-the-world gRPC service that
// carries it.
const Protobuf::MethodDescriptor& sotwGrpcMethod(absl::string_view type_url);
// Translates an xDS resource type_url to the name of the REST service that carries it.
const Protobuf::MethodDescriptor& restMethod(absl::string_view type_url);

} // namespace Server
} // namespace Envoy
