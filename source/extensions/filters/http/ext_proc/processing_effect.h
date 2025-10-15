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
    // No processing effect. This is the default value except for body processing mode
    // FULL_DUPLEX_STREAMED.
    None,
    // The processor response sent a mutation that modified the body or headers.
    // This is the dafualt value for body requests/responses using
    // FULL_DUPLEX_STREAMED processing mode.
    MutationApplied,
    // The processor response sent a mutation that was attempted to modify the body or headers but
    // was not applied due to failure or ignored.
    MutationRejected,
  };
};
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
