#include "test/mocks/common.h"

namespace Lyft {
ReadyWatcher::ReadyWatcher() {}
ReadyWatcher::~ReadyWatcher() {}

MockSystemTimeSource::MockSystemTimeSource() {}
MockSystemTimeSource::~MockSystemTimeSource() {}

MockMonotonicTimeSource::MockMonotonicTimeSource() {}
MockMonotonicTimeSource::~MockMonotonicTimeSource() {}
} // Lyft