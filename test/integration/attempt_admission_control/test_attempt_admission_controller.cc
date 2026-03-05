#include "test/integration/attempt_admission_control/test_attempt_admission_controller.h"

#include "envoy/registry/registry.h"

namespace Envoy {

REGISTER_FACTORY(TestAttemptAdmissionControllerFactory,
                 Upstream::AttemptAdmissionControllerFactory);

} // namespace Envoy
