#include "test/mocks/common.h"

namespace Envoy {
ReadyWatcher::ReadyWatcher() {}
ReadyWatcher::~ReadyWatcher() {}

MockSystemTimeSource::MockSystemTimeSource() {}
MockSystemTimeSource::~MockSystemTimeSource() {}

MockMonotonicTimeSource::MockMonotonicTimeSource() {}
MockMonotonicTimeSource::~MockMonotonicTimeSource() {}
} // namespace Envoy
