#include "test/mocks/common.h"

namespace Envoy {
ReadyWatcher::ReadyWatcher() {}
ReadyWatcher::~ReadyWatcher() {}

MockSystemTimeSource::MockSystemTimeSource() {}
MockSystemTimeSource::~MockSystemTimeSource() {}

MockMonotonicTimeSource::MockMonotonicTimeSource() {}
MockMonotonicTimeSource::~MockMonotonicTimeSource() {}

MockTimeSource::MockTimeSource() : TimeSource(system_, monotonic_) {}
MockTimeSource::~MockTimeSource() {}

MockTokenBucket::MockTokenBucket() {}
MockTokenBucket::~MockTokenBucket() {}

} // namespace Envoy
