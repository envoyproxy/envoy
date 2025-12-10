#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

REGISTER_FACTORY(TestProcessingRequestModifierFactory, ProcessingRequestModifierFactory);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
