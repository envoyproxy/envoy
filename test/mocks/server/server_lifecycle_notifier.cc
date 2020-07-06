#include "server_lifecycle_notifier.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

MockServerLifecycleNotifier::MockServerLifecycleNotifier() = default;

MockServerLifecycleNotifier::~MockServerLifecycleNotifier() = default;

} // namespace Server
} // namespace Envoy
