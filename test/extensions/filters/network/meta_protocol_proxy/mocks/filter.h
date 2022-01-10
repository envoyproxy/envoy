#pragma once

#include "source/extensions/filters/network/meta_protocol_proxy/interface/config.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/filter.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

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
  MOCK_METHOD(std::string, configType, ());
  MOCK_METHOD(bool, isTerminalFilter, ());
};

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks() = default;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr filter));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr filter));
  MOCK_METHOD(void, addFilter, (StreamFilterSharedPtr filter));
};

template <class Base> class MockStreamFilterCallbacks : public Base {
public:
  MOCK_METHOD(Envoy::Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(const CodecFactory&, downstreamCodec, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
};

class MockDecoderFilterCallback : public MockStreamFilterCallbacks<DecoderFilterCallback> {
public:
  MOCK_METHOD(void, sendLocalReply, (Status, absl::string_view, ResponseUpdateFunction&&));
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(void, upstreamResponse, (ResponsePtr response));
  MOCK_METHOD(void, completeDirectly, ());
};

class MockEncoderFilterCallback : public MockStreamFilterCallbacks<EncoderFilterCallback> {
public:
  MOCK_METHOD(void, continueEncoding, ());
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
