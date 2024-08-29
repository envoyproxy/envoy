#pragma once

#include "source/extensions/filters/network/generic_proxy/interface/filter.h"

#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class MockRequestFramesHandler : public RequestFramesHandler {
public:
  MockRequestFramesHandler();

  MOCK_METHOD(void, onRequestCommonFrame, (RequestCommonFramePtr));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(HeaderFilterStatus, decodeHeaderFrame, (RequestHeaderFrame&));
  MOCK_METHOD(CommonFilterStatus, decodeCommonFrame, (RequestCommonFrame&));

  DecoderFilterCallback* decoder_callbacks_{};
};

class MockEncoderFilter : public EncoderFilter {
public:
  MockEncoderFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(HeaderFilterStatus, encodeHeaderFrame, (ResponseHeaderFrame&));
  MOCK_METHOD(CommonFilterStatus, encodeCommonFrame, (ResponseCommonFrame&));

  EncoderFilterCallback* encoder_callbacks_{};
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(HeaderFilterStatus, decodeHeaderFrame, (RequestHeaderFrame&));
  MOCK_METHOD(CommonFilterStatus, decodeCommonFrame, (RequestCommonFrame&));

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(HeaderFilterStatus, encodeHeaderFrame, (ResponseHeaderFrame&));
  MOCK_METHOD(CommonFilterStatus, encodeCommonFrame, (ResponseCommonFrame&));

  DecoderFilterCallback* decoder_callbacks_{};
  EncoderFilterCallback* encoder_callbacks_{};
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
  MOCK_METHOD(absl::Status, validateCodec, (const TypedExtensionConfig&));
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
  MOCK_METHOD(const CodecFactory&, codecFactory, ());
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
};

class MockDecoderFilterCallback : public MockStreamFilterCallbacks<DecoderFilterCallback> {
public:
  MockDecoderFilterCallback();

  MOCK_METHOD(void, sendLocalReply, (Status, absl::string_view, ResponseUpdateFunction));
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(void, onResponseHeaderFrame, (ResponseHeaderFramePtr));
  MOCK_METHOD(void, onResponseCommonFrame, (ResponseCommonFramePtr));
  MOCK_METHOD(void, setRequestFramesHandler, (RequestFramesHandler*));
  MOCK_METHOD(void, completeDirectly, ());
  MOCK_METHOD(absl::string_view, filterConfigName, (), (const));
};

class MockEncoderFilterCallback : public MockStreamFilterCallbacks<EncoderFilterCallback> {
public:
  MOCK_METHOD(void, continueEncoding, ());
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
