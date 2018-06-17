#include "test/extensions/filters/network/thrift_proxy/mocks.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

MockTransportCallbacks::MockTransportCallbacks() {}
MockTransportCallbacks::~MockTransportCallbacks() {}

MockTransport::MockTransport() { ON_CALL(*this, name()).WillByDefault(ReturnRef(name_)); }
MockTransport::~MockTransport() {}

MockProtocolCallbacks::MockProtocolCallbacks() {}
MockProtocolCallbacks::~MockProtocolCallbacks() {}

MockProtocol::MockProtocol() { ON_CALL(*this, name()).WillByDefault(ReturnRef(name_)); }
MockProtocol::~MockProtocol() {}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
