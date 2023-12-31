#include "test/integration/retry_admission_control/custom_retry_admission_controller.h"

#include "envoy/registry/registry.h"

namespace Envoy {

REGISTER_FACTORY(CustomRetryAdmissionControllerFactory, Upstream::RetryAdmissionControllerFactory);

} // namespace Envoy
