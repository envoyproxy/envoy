#include "contrib/sip_proxy/filters/network/test/mocks.h"

#include <memory>

#include "source/common/protobuf/protobuf.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {

// Provide a specialization for ProtobufWkt::Struct (for MockFilterConfigFactory)
template <>
void MessageUtil::validate(const ProtobufWkt::Struct&, ProtobufMessage::ValidationVisitor&, bool) {}

namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

MockConfig::MockConfig() = default;
MockConfig::~MockConfig() = default;

MockDecoderCallbacks::MockDecoderCallbacks() = default;
MockDecoderCallbacks::~MockDecoderCallbacks() = default;

MockDecoderEventHandler::MockDecoderEventHandler() {
  ON_CALL(*this, transportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageEnd()).WillByDefault(Return(FilterStatus::Continue));
}
MockDecoderEventHandler::~MockDecoderEventHandler() = default;

MockDirectResponse::MockDirectResponse() = default;
MockDirectResponse::~MockDirectResponse() = default;

namespace SipFilters {

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, transportBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, transportEnd()).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageBegin(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, messageEnd()).WillByDefault(Return(FilterStatus::Continue));
}
MockDecoderFilter::~MockDecoderFilter() = default;

MockDecoderFilterCallbacks::MockDecoderFilterCallbacks()
    : stats_(SipFilterStats::generateStats("test", store_)) {

  ON_CALL(*this, streamId()).WillByDefault(Return(stream_id_));
  ON_CALL(*this, transactionInfos()).WillByDefault(Return(transaction_infos_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(connection_.stream_info_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
}
MockDecoderFilterCallbacks::~MockDecoderFilterCallbacks() = default;

MockFilterConfigFactory::MockFilterConfigFactory() : name_("envoy.filters.sip.mock_filter") {
  mock_filter_ = std::make_shared<NiceMock<MockDecoderFilter>>();
}

MockFilterConfigFactory::~MockFilterConfigFactory() = default;

FilterFactoryCb MockFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(context);

  config_struct_ = dynamic_cast<const ProtobufWkt::Struct&>(proto_config);
  config_stat_prefix_ = stats_prefix;

  return [this](FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(mock_filter_);
  };
}

} // namespace SipFilters
//
namespace Router {

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
}
MockRouteEntry::~MockRouteEntry() = default;

MockRoute::MockRoute() { ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_)); }
MockRoute::~MockRoute() = default;

} // namespace Router

MockConnectionManager::~MockConnectionManager() = default;

MockTrafficRoutingAssistantHandler::MockTrafficRoutingAssistantHandler(
    ConnectionManager& parent, Event::Dispatcher& dispatcher,
    const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig& config,
    Server::Configuration::FactoryContext& context, StreamInfo::StreamInfoImpl& stream_info)
    : TrafficRoutingAssistantHandler(parent, dispatcher, config, context, stream_info) {
  ON_CALL(*this, retrieveTrafficRoutingAssistant(_, _, _, _, _))
      .WillByDefault(
          Invoke([&](const std::string&, const std::string&, const absl::optional<TraContextMap>,
                     SipFilters::DecoderFilterCallbacks&, std::string& host) -> QueryStatus {
            host = "10.0.0.11";
            return QueryStatus::Continue;
          }));
}

MockTrafficRoutingAssistantHandler::~MockTrafficRoutingAssistantHandler() = default;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
