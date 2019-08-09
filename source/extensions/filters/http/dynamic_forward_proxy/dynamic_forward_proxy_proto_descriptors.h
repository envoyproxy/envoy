#pragma once

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

// This function validates that the message descriptors that
// are referenced in Any messages are available in the descriptor pool.
bool validateProtoDescriptors();

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
