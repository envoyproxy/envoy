#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"

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
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](DecoderFilterCallback& cb) { decoder_callbacks_ = &cb; }));

  ON_CALL(*this, decodeHeaderFrame(_)).WillByDefault(Return(HeaderFilterStatus::Continue));
  ON_CALL(*this, decodeCommonFrame(_)).WillByDefault(Return(CommonFilterStatus::Continue));
}

MockEncoderFilter::MockEncoderFilter() {
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](EncoderFilterCallback& cb) { encoder_callbacks_ = &cb; }));

  ON_CALL(*this, encodeHeaderFrame(_)).WillByDefault(Return(HeaderFilterStatus::Continue));
  ON_CALL(*this, encodeCommonFrame(_)).WillByDefault(Return(CommonFilterStatus::Continue));
}

MockStreamFilter::MockStreamFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](DecoderFilterCallback& cb) { decoder_callbacks_ = &cb; }));
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](EncoderFilterCallback& cb) { encoder_callbacks_ = &cb; }));

  ON_CALL(*this, decodeHeaderFrame(_)).WillByDefault(Return(HeaderFilterStatus::Continue));
  ON_CALL(*this, decodeCommonFrame(_)).WillByDefault(Return(CommonFilterStatus::Continue));
  ON_CALL(*this, encodeHeaderFrame(_)).WillByDefault(Return(HeaderFilterStatus::Continue));
  ON_CALL(*this, encodeCommonFrame(_)).WillByDefault(Return(CommonFilterStatus::Continue));
}

MockDecoderFilterCallback::MockDecoderFilterCallback() = default;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
