#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamDecoded, (Request & request));
};

class MockEncoderFilter : public EncoderFilter {
public:
  MockEncoderFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamEncoded, (Response & response));
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamEncoded, (Response & response));

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamDecoded, (Request & request));
};

class MockStreamFilterConfig : public NamedFilterConfigFactory {
public:
  MockStreamFilterConfig();

  MOCK_METHOD(FilterFactoryCb, createFilterFactoryFromProto,
              (const Protobuf::Message& config, const std::string& stat_prefix,
               Server::Configuration::FactoryContext& context));
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, ());
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyRouteConfigProto, ());
  MOCK_METHOD(RouteSpecificFilterConfigConstSharedPtr, createRouteSpecificFilterConfig,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
               ProtobufMessage::ValidationVisitor&));
  MOCK_METHOD(std::string, name, (), (const));
  MOCK_METHOD(std::set<std::string>, configTypes, ());
  MOCK_METHOD(bool, isTerminalFilter, ());
};

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks() = default;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr filter));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr filter));
  MOCK_METHOD(void, addFilter, (StreamFilterSharedPtr filter));
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();

  MOCK_METHOD(void, applyFilterFactoryCb, (FilterContext context, FilterFactoryCb& factory));

  testing::NiceMock<MockFilterChainFactoryCallbacks> callbacks_;
  std::vector<FilterContext> contexts_;
};

template <class Base> class MockStreamFilterCallbacks : public Base {
public:
  MOCK_METHOD(Envoy::Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(const CodecFactory&, downstreamCodec, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (), (const));
};

class MockDecoderFilterCallback : public MockStreamFilterCallbacks<DecoderFilterCallback> {
public:
  MOCK_METHOD(void, sendLocalReply, (Status, ResponseUpdateFunction&&));
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(void, upstreamResponse, (ResponsePtr response));
  MOCK_METHOD(void, completeDirectly, ());
};

class MockEncoderFilterCallback : public MockStreamFilterCallbacks<EncoderFilterCallback> {
public:
  MOCK_METHOD(void, continueEncoding, ());
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
