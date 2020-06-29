#include "listener_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
MockListenerManager::MockListenerManager() = default;

MockListenerManager::~MockListenerManager() = default;

} // namespace Server

} // namespace Envoy
