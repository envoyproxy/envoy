#include "test/extensions/filters/network/dubbo_proxy/mocks.h"

#include <memory>

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

MockStreamDecoder::MockStreamDecoder() {
  ON_CALL(*this, onMessageDecoded(_, _)).WillByDefault(Return(FilterStatus::Continue));
}

MockStreamEncoder::MockStreamEncoder() {
  ON_CALL(*this, onMessageEncoded(_, _)).WillByDefault(Return(FilterStatus::Continue));
}

MockRequestDecoderCallbacks::MockRequestDecoderCallbacks() {
  ON_CALL(*this, newStream()).WillByDefault(ReturnRef(handler_));
}

MockResponseDecoderCallbacks::MockResponseDecoderCallbacks() {
  ON_CALL(*this, newStream()).WillByDefault(ReturnRef(handler_));
}

MockProtocol::MockProtocol() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
  ON_CALL(*this, serializer()).WillByDefault(Return(&serializer_));
}
MockProtocol::~MockProtocol() = default;

MockSerializer::MockSerializer() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
}
MockSerializer::~MockSerializer() = default;

namespace DubboFilters {

MockFilterChainFactory::MockFilterChainFactory() = default;
MockFilterChainFactory::~MockFilterChainFactory() = default;

MockFilterChainFactoryCallbacks::MockFilterChainFactoryCallbacks() = default;
MockFilterChainFactoryCallbacks::~MockFilterChainFactoryCallbacks() = default;

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(
          Invoke([this](DecoderFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}
MockDecoderFilter::~MockDecoderFilter() = default;

MockDecoderFilterCallbacks::MockDecoderFilterCallbacks() {
  route_ = std::make_shared<NiceMock<Router::MockRoute>>();

  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, connection()).WillByDefault(Return(&connection_));
  ON_CALL(*this, route()).WillByDefault(Return(route_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}
MockDecoderFilterCallbacks::~MockDecoderFilterCallbacks() = default;

MockEncoderFilter::MockEncoderFilter() {
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(
          Invoke([this](EncoderFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}
MockEncoderFilter::~MockEncoderFilter() = default;

MockEncoderFilterCallbacks::MockEncoderFilterCallbacks() {
  route_ = std::make_shared<NiceMock<Router::MockRoute>>();

  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, connection()).WillByDefault(Return(&connection_));
  ON_CALL(*this, route()).WillByDefault(Return(route_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}
MockEncoderFilterCallbacks::~MockEncoderFilterCallbacks() = default;

MockCodecFilter::MockCodecFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke(
          [this](DecoderFilterCallbacks& callbacks) -> void { decoder_callbacks_ = &callbacks; }));
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke(
          [this](EncoderFilterCallbacks& callbacks) -> void { encoder_callbacks_ = &callbacks; }));
}
MockCodecFilter::~MockCodecFilter() = default;

MockFilterConfigFactory::MockFilterConfigFactory()
    : MockFactoryBase("envoy.filters.dubbo.mock_filter"),
      mock_filter_(std::make_shared<NiceMock<MockDecoderFilter>>()) {}

MockFilterConfigFactory::~MockFilterConfigFactory() = default;

FilterFactoryCb
MockFilterConfigFactory::createFilterFactoryFromProtoTyped(const ProtobufWkt::Struct& proto_config,
                                                           const std::string& stat_prefix,
                                                           Server::Configuration::FactoryContext&) {
  config_struct_ = proto_config;
  config_stat_prefix_ = stat_prefix;

  return [this](DubboFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(mock_filter_);
  };
}

} // namespace DubboFilters

namespace Router {

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
}
MockRouteEntry::~MockRouteEntry() = default;

MockRoute::MockRoute() { ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_)); }
MockRoute::~MockRoute() = default;

} // namespace Router

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
