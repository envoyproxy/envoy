#pragma once

#include "envoy/router/router.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/printers.h"

#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
#include "contrib/sip_proxy/filters/network/source/conn_state.h"
#include "contrib/sip_proxy/filters/network/source/filters/factory_base.h"
#include "contrib/sip_proxy/filters/network/source/filters/filter.h"
#include "contrib/sip_proxy/filters/network/source/metadata.h"
#include "contrib/sip_proxy/filters/network/source/protocol.h"
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
  MOCK_METHOD(DecoderPtr, createDecoder, (DecoderCallbacks&));
  MOCK_METHOD(Router::Config&, routerConfig, ());
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

  // SipProxy::DecoderCallbacks
  MOCK_METHOD(DecoderEventHandler&, newDecoderEventHandler, (MessageMetadataSharedPtr));
  MOCK_METHOD(absl::string_view, getLocalIp, ());
  MOCK_METHOD(std::string, getOwnDomain, ());
  MOCK_METHOD(std::string, getDomainMatchParamName, ());
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
  MOCK_METHOD(void, resetUpstreamConnection, ());
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
  MOCK_METHOD(std::shared_ptr<SipProxy::SipSettings>, settings, ());
  MOCK_METHOD(void, onReset, ());
  MOCK_METHOD(MessageMetadataSharedPtr, responseMetadata, ());
  MOCK_METHOD(bool, responseSuccess, ());

  uint64_t stream_id_{1};
  std::string transaction_id_{"test"};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
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

  //  // SipProxy::Router::RouteEntry
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
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
