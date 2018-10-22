#include "test/extensions/filters/network/dubbo_proxy/mocks.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

MockProtocolCallbacks::MockProtocolCallbacks() {}
MockProtocolCallbacks::~MockProtocolCallbacks() {}

MockProtocol::MockProtocol() { ON_CALL(*this, name()).WillByDefault(ReturnRef(name_)); }
MockProtocol::~MockProtocol() {}

MockDeserializer::MockDeserializer() { ON_CALL(*this, name()).WillByDefault(ReturnRef(name_)); }
MockDeserializer::~MockDeserializer() {}
MockDeserializationCallbacks::MockDeserializationCallbacks() {}
MockDeserializationCallbacks::~MockDeserializationCallbacks() {}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy