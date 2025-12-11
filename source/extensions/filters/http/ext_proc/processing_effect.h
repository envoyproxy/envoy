#pragma once

#include <memory>
#include <string>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ProcessingEffect {
public:
  // The processing effect that was applied by the external processor.
  enum class Effect : uint8_t {
    // No processing effect. This is the default value except for body request/responses with body
    // processing mode
    // FULL_DUPLEX_STREAMED. In this case MutationApplied if the default.
    None,
    // The processor response sent a mutation that successfully modified the body or headers.
    // This is the default value for body requests/responses using
    // FULL_DUPLEX_STREAMED processing mode.
    MutationApplied,
    // The processor response sent a mutation that was attempted to modify the headers or trailers
    // but was not applied due to invalid name or value.
    InvalidMutationRejected,
    // The processor response sent a mutation that was attempted to modify the headers or trailers
    // but was not applied due to size limit exceeded.
    MutationRejectedSizeLimitExceeded,
    // The processor response sent a mutation that was attempted to modify the headers or trailers
    // but was not applied due to failure.
    MutationFailed,
  };
};
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
