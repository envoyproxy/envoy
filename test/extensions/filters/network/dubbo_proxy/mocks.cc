#include "test/extensions/filters/network/dubbo_proxy/mocks.h"

#include <memory>

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

MockDecoderEventHandler::MockDecoderEventHandler() {
  ON_CALL(*this, transportBegin()).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_, _, _)).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, messageEnd(_)).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, transferHeaderTo(_, _)).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, transferBodyTo(_, _)).WillByDefault(Return(Network::FilterStatus::Continue));
}

MockDecoderCallbacks::MockDecoderCallbacks() {
  ON_CALL(*this, newDecoderEventHandler()).WillByDefault(Return(&handler_));
}

MockProtocol::MockProtocol() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
}
MockProtocol::~MockProtocol() = default;

MockDeserializer::MockDeserializer() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, type()).WillByDefault(Return(type_));
}
MockDeserializer::~MockDeserializer() = default;

namespace DubboFilters {

MockFilterChainFactoryCallbacks::MockFilterChainFactoryCallbacks() = default;
MockFilterChainFactoryCallbacks::~MockFilterChainFactoryCallbacks() = default;

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, transportBegin()).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_, _, _)).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, messageEnd(_)).WillByDefault(Return(Network::FilterStatus::Continue));
  ON_CALL(*this, transferHeaderTo(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& buf, size_t size) -> Network::FilterStatus {
        buf.drain(size);
        return Network::FilterStatus::Continue;
      }));
  ON_CALL(*this, transferBodyTo(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& buf, size_t size) -> Network::FilterStatus {
        buf.drain(size);
        return Network::FilterStatus::Continue;
      }));
}
MockDecoderFilter::~MockDecoderFilter() = default;

MockDecoderFilterCallbacks::MockDecoderFilterCallbacks() {
  route_.reset(new NiceMock<Router::MockRoute>());

  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, connection()).WillByDefault(Return(&connection_));
  ON_CALL(*this, route()).WillByDefault(Return(route_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
}
MockDecoderFilterCallbacks::~MockDecoderFilterCallbacks() = default;

MockDirectResponse::MockDirectResponse() = default;
MockDirectResponse::~MockDirectResponse() = default;

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
