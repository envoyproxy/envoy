#include "test/extensions/filters/network/thrift_proxy/mocks.h"

#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

MockConfig::MockConfig() {}
MockConfig::~MockConfig() {}

MockTransport::MockTransport() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
}
MockTransport::~MockTransport() {}

MockProtocol::MockProtocol() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
}
MockProtocol::~MockProtocol() {}

MockDecoderCallbacks::MockDecoderCallbacks() {}
MockDecoderCallbacks::~MockDecoderCallbacks() {}

namespace ThriftFilters {

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, transportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, structBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, structEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, fieldBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, fieldEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, boolValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, byteValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int16Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int32Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, int64Value(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, doubleValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, stringValue(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, mapBegin(_, _, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, mapEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, listBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, listEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, setBegin(_, _)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, setEnd()).WillByDefault(Return(FilterStatus::Continue));
}
MockDecoderFilter::~MockDecoderFilter() {}

MockDecoderFilterCallbacks::MockDecoderFilterCallbacks() {
  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, connection()).WillByDefault(Return(&connection_));
}
MockDecoderFilterCallbacks::~MockDecoderFilterCallbacks() {}

} // namespace ThriftFilters

namespace Router {

MockRouteEntry::MockRouteEntry() {}
MockRouteEntry::~MockRouteEntry() {}

MockRoute::MockRoute() {}
MockRoute::~MockRoute() {}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
