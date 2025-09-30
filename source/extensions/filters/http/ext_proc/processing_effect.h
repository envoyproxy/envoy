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
  enum class Effect: char {
    // No processing effect.
    None,
    // The processor response sent a mutation that modified the body or headers.
    // This is always true for body requests/responses using
    // FULL_DUPLEX_STREAMED processing mode.
    ContentModified,
    // An immediate response was sent.
    ImmediateResponse,
    // The processor response sent a mutation that sent status code
    // CONTINUE_AND_REPLACE to process body mutatins.
    // This is only used for header requests/responses.
    ContinueAndReplace
  };
};
}  // namespace ExternalProcessing
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
