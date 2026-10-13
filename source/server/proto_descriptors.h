#pragma once

namespace Envoy {
namespace Server {

// This function validates that the method descriptors for gRPC services and type descriptors that
// are referenced in Any messages are available in the descriptor pool.
void validateProtoDescriptors();
} // namespace Server
} // namespace Envoy
