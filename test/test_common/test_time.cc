#include "test/test_common/test_time.h"

#include "common/common/utility.h"

namespace Envoy {

DangerousDeprecatedTestTime::DangerousDeprecatedTestTime() {}

namespace Event {

void TestRealTimeSystem::sleep(const Duration& duration) { std::this_thread::sleep_for(duration); }

} // namespace Event
} // namespace Envoy
