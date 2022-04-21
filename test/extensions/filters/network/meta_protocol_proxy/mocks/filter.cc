#include "test/extensions/filters/network/meta_protocol_proxy/mocks/filter.h"

#include "source/common/protobuf/protobuf.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

MockStreamFilterConfig::MockStreamFilterConfig() {
  ON_CALL(*this, createEmptyConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
  ON_CALL(*this, createFilterFactoryFromProto(_, _, _))
      .WillByDefault(Return([](FilterChainFactoryCallbacks&) {}));
  ON_CALL(*this, name()).WillByDefault(Return("envoy.filters.meta_protocol.mock_filter"));
  ON_CALL(*this, configType()).WillByDefault(Return(""));
}

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, onStreamDecoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

MockEncoderFilter::MockEncoderFilter() {
  ON_CALL(*this, onStreamEncoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

MockStreamFilter::MockStreamFilter() {
  ON_CALL(*this, onStreamEncoded(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, onStreamDecoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
