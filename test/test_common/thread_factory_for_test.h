#pragma once

#include "include/envoy/thread/thread.h"

namespace Envoy {

namespace Thread {
ThreadFactory& threadFactoryForTest();
} // namespace Thread

} // namespace Envoy
