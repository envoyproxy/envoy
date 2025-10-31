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
  enum class Effect : char {
    // No processing effect. This is the default value except when body processing mode is
    // FULL_DUPLEX_STREAMED.
    None,
    // The processor response sent a mutation that successfully modified the body or headers.
    // This is the dafualt value for body requests/responses using
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
    // Some mutations were applied while other mutations were rejected.
    // This can arise when multiple GRPC messages attempt to mutate the same part of the HTTP
    // request/repsonse.
    PartialMutationsApplied,
  };
};
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
