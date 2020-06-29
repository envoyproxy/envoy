#include "filter_chain_factory_context.h"

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
namespace Configuration {
MockFilterChainFactoryContext::MockFilterChainFactoryContext() = default;

MockFilterChainFactoryContext::~MockFilterChainFactoryContext() = default;

} // namespace Configuration

} // namespace Server

} // namespace Envoy
