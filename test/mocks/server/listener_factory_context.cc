#include "listener_factory_context.h"

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
namespace Configuration {
MockListenerFactoryContext::MockListenerFactoryContext() = default;

MockListenerFactoryContext::~MockListenerFactoryContext() = default;

} // namespace Configuration

} // namespace Server

} // namespace Envoy
