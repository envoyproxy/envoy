#include "test/test_common/test_time.h"

#include "common/common/utility.h"

namespace Envoy {

DangerousDeprecatedTestTime::DangerousDeprecatedTestTime()
    : time_source_(system_time_, monotonic_time_) {}

} // namespace Envoy
