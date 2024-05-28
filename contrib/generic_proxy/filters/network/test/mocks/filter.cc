#include "contrib/generic_proxy/filters/network/test/mocks/filter.h"

#include <cstdint>

#include "source/common/protobuf/protobuf.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

MockRequestFramesHandler::MockRequestFramesHandler() = default;

MockStreamFilterConfig::MockStreamFilterConfig() {
  ON_CALL(*this, createEmptyConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
  ON_CALL(*this, createFilterFactoryFromProto(_, _, _))
      .WillByDefault(Return([](FilterChainFactoryCallbacks&) {}));
  ON_CALL(*this, name()).WillByDefault(Return("envoy.filters.generic.mock_filter"));
  ON_CALL(*this, configTypes()).WillByDefault(Invoke([this]() {
    return NamedFilterConfigFactory::configTypes();
  }));
}

MockFilterChainManager::MockFilterChainManager() {
  ON_CALL(*this, applyFilterFactoryCb(_, _))
      .WillByDefault(Invoke([this](FilterContext context, FilterFactoryCb& factory) {
        contexts_.push_back(context);
        factory(callbacks_);
      }));
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

MockDecoderFilterCallback::MockDecoderFilterCallback() = default;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
