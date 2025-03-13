#pragma once

#include <memory>
#include <string>

#include "envoy/router/router.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
#include "contrib/sip_proxy/filters/network/source/conn_state.h"
#include "contrib/sip_proxy/filters/network/source/decoder_events.h"
#include "contrib/sip_proxy/filters/network/source/filters/factory_base.h"
#include "contrib/sip_proxy/filters/network/source/filters/filter.h"
#include "contrib/sip_proxy/filters/network/source/metadata.h"
#include "contrib/sip_proxy/filters/network/source/router/router.h"
#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override;

  // SipProxy::Config
  MOCK_METHOD(SipFilters::FilterChainFactory&, filterFactory, ());
  MOCK_METHOD(SipFilterStats&, stats, ());
  MOCK_METHOD(Router::Config&, routerConfig, ());
  MOCK_METHOD(std::shared_ptr<SipSettings>, settings, ());
};

class MockRouterFilterConfig : public Router::RouterFilterConfig {
public:
  // Router::RouterFilterConfig
  MOCK_METHOD(Router::RouterStats&, stats, ());
};

class MockDecoderEventHandler : public DecoderEventHandler {
public:
  MockDecoderEventHandler();
  ~MockDecoderEventHandler() override;

  // SipProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
};

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MockDecoderCallbacks();
  ~MockDecoderCallbacks() override;

  MOCK_METHOD(DecoderEventHandler&, newDecoderEventHandler, (MessageMetadataSharedPtr));
  MOCK_METHOD(std::shared_ptr<SipProxy::SipSettings>, settings, (), (const));

  std::shared_ptr<SipProxy::SipSettings> settings_;
};

class MockDirectResponse : public DirectResponse {
public:
  MockDirectResponse();
  ~MockDirectResponse() override;

  // SipProxy::DirectResponse
  MOCK_METHOD(DirectResponse::ResponseType, encode,
              (MessageMetadata & metadata, Buffer::Instance& buffer), (const));
};

namespace Router {
class MockRoute;
} // namespace Router

namespace SipFilters {

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();
  ~MockDecoderFilter() override;

  // SipProxy::SipFilters::DecoderFilter
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallbacks & callbacks));
  MOCK_METHOD(bool, passthroughSupported, (), (const));

  // SipProxy::DecoderEventHandler
  MOCK_METHOD(FilterStatus, passthroughData, (Buffer::Instance & data));
  MOCK_METHOD(FilterStatus, transportBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, transportEnd, ());
  MOCK_METHOD(FilterStatus, messageBegin, (MessageMetadataSharedPtr metadata));
  MOCK_METHOD(FilterStatus, messageEnd, ());
};

class MockDecoderFilterCallbacks : public DecoderFilterCallbacks {
public:
  MockDecoderFilterCallbacks();
  ~MockDecoderFilterCallbacks() override;

  // SipProxy::SipFilters::DecoderFilterCallbacks
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(std::string, transactionId, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, sendLocalReply, (const DirectResponse&, bool));
  MOCK_METHOD(void, startUpstreamResponse, ());
  MOCK_METHOD(ResponseStatus, upstreamData, (MessageMetadataSharedPtr));
  MOCK_METHOD(void, resetDownstreamConnection, ());
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(std::shared_ptr<Router::TransactionInfos>, transactionInfos, ());
  MOCK_METHOD(std::shared_ptr<SipProxy::SipSettings>, settings, (), (const));
  MOCK_METHOD(std::shared_ptr<SipProxy::TrafficRoutingAssistantHandler>, traHandler, ());
  MOCK_METHOD(void, onReset, ());
  MOCK_METHOD(void, pushIntoPendingList,
              (const std::string&, const std::string&, SipFilters::DecoderFilterCallbacks&,
               std::function<void(void)>));
  MOCK_METHOD(void, onResponseHandleForPendingList,
              (const std::string&, const std::string&,
               std::function<void(MessageMetadataSharedPtr, DecoderEventHandler&)>));
  MOCK_METHOD(void, eraseActiveTransFromPendingList, (std::string&));
  MOCK_METHOD(void, continueHandling, (const std::string&, bool));
  MOCK_METHOD(MessageMetadataSharedPtr, metadata, ());
  MOCK_METHOD(SipFilterStats&, stats, ());

  uint64_t stream_id_{1};
  std::string transaction_id_{"test"};
  NiceMock<Network::MockConnection> connection_;
  std::shared_ptr<Router::MockRoute> route_;
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  Stats::TestUtil::TestStore store_;
  SipFilterStats stats_;
};

class MockFilterConfigFactory : public NamedSipFilterConfigFactory {
public:
  MockFilterConfigFactory();
  ~MockFilterConfigFactory() override;

  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return name_; }

  ProtobufWkt::Struct config_struct_;
  std::string config_stat_prefix_;

private:
  std::shared_ptr<MockDecoderFilter> mock_filter_;
  const std::string name_;
};

} // namespace SipFilters

namespace Router {

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();
  ~MockRouteEntry() override;

  // SipProxy::Router::RouteEntry
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(const Envoy::Router::MetadataMatchCriteria*, metadataMatchCriteria, (), (const));
  std::string cluster_name_{"fake_cluster"};
};

class MockRoute : public Route {
public:
  MockRoute();
  ~MockRoute() override;

  // SipProxy::Router::Route
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));

  NiceMock<MockRouteEntry> route_entry_;
};
} // namespace Router

class MockTrafficRoutingAssistantHandler : public TrafficRoutingAssistantHandler {
public:
  MockTrafficRoutingAssistantHandler(
      ConnectionManager& parent, Event::Dispatcher& dispatcher,
      const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig& config,
      Server::Configuration::FactoryContext& context, StreamInfo::StreamInfoImpl& stream_info);
  MOCK_METHOD(void, updateTrafficRoutingAssistant,
              (const std::string&, const std::string&, const std::string&,
               const absl::optional<TraContextMap>),
              ());
  MOCK_METHOD(QueryStatus, retrieveTrafficRoutingAssistant,
              (const std::string&, const std::string&, const absl::optional<TraContextMap>,
               SipFilters::DecoderFilterCallbacks&, std::string&),
              ());
  MOCK_METHOD(void, deleteTrafficRoutingAssistant,
              (const std::string&, const std::string&, const absl::optional<TraContextMap>), ());
  MOCK_METHOD(void, subscribeTrafficRoutingAssistant, (const std::string&), ());
  MOCK_METHOD(void, doSubscribe,
              (const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity),
              ());

  // TODO(wbpcode): mock this method will cause the clang-tidy error. To avoid this error and
  // updating the source code, we will not mock this method for now. It is OK because this method is
  // not used in the unit test. If someone wants to mock this method, please update the source code
  // to avoid using the absl::any.
  void complete(const TrafficRoutingAssistant::ResponseType&, const std::string&,
                const absl::any&) override {}

  ~MockTrafficRoutingAssistantHandler() override;
};

class MockConnectionManager : public ConnectionManager {
public:
  MockConnectionManager(const ConfigSharedPtr& config, Random::RandomGenerator& random_generator,
                        TimeSource& time_system, Server::Configuration::FactoryContext& context,
                        std::shared_ptr<Router::TransactionInfos> transaction_infos)
      : ConnectionManager(config, random_generator, time_system, context, transaction_infos) {}

  ~MockConnectionManager() override;

  MOCK_METHOD(std::shared_ptr<TrafficRoutingAssistantHandler>, traHandler, ());
  MOCK_METHOD(std::shared_ptr<SipSettings>, settings, (), (const));
};

class MockTrafficRoutingAssistantHandlerDeep : public TrafficRoutingAssistantHandler {
public:
  MockTrafficRoutingAssistantHandlerDeep(
      ConnectionManager& parent, Event::Dispatcher& dispatcher,
      const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig& config,
      Server::Configuration::FactoryContext& context, StreamInfo::StreamInfoImpl& stream_info)
      : TrafficRoutingAssistantHandler(parent, dispatcher, config, context, stream_info) {}
  MOCK_METHOD(TrafficRoutingAssistant::ClientPtr&, traClient, (), (override));
};

class MockRequestCallbacks : public TrafficRoutingAssistant::RequestCallbacks {
public:
  // TODO(wbpcode): mock this method will cause the clang-tidy error. To avoid this error and
  // updating the source code, we will not mock this method for now. It is OK because this method is
  // not used in the unit test. If someone wants to mock this method, please update the source code
  // to avoid using the absl::any.
  void complete(const TrafficRoutingAssistant::ResponseType&, const std::string&,
                const absl::any&) override {}
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
