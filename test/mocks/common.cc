#include "test/mocks/common.h"

namespace Envoy {
ReadyWatcher::ReadyWatcher() {}
ReadyWatcher::~ReadyWatcher() {}

MockTimeSource::MockTimeSource() {}
MockTimeSource::~MockTimeSource() {}

MockTimeSource::MockTimeSource() : TimeSource(system_, monotonic_) {}
MockTimeSource::~MockTimeSource() {}

MockTokenBucket::MockTokenBucket() {}
MockTokenBucket::~MockTokenBucket() {}

} // namespace Envoy
