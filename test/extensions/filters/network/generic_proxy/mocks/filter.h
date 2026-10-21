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

  MOCK_METHOD(void, onRequestCommonFrame, (RequestCommonFramePtr), (override));
};

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();

  MOCK_METHOD(void, onDestroy, (), (override));

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks), (override));
  MOCK_METHOD(HeaderFilterStatus, decodeHeaderFrame, (RequestHeaderFrame&), (override));
  MOCK_METHOD(CommonFilterStatus, decodeCommonFrame, (RequestCommonFrame&), (override));

  DecoderFilterCallback* decoder_callbacks_{};
};

class MockEncoderFilter : public EncoderFilter {
public:
  MockEncoderFilter();

  MOCK_METHOD(void, onDestroy, (), (override));

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks), (override));
  MOCK_METHOD(HeaderFilterStatus, encodeHeaderFrame, (ResponseHeaderFrame&), (override));
  MOCK_METHOD(CommonFilterStatus, encodeCommonFrame, (ResponseCommonFrame&), (override));

  EncoderFilterCallback* encoder_callbacks_{};
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();

  MOCK_METHOD(void, onDestroy, (), (override));

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks), (override));
  MOCK_METHOD(HeaderFilterStatus, decodeHeaderFrame, (RequestHeaderFrame&), (override));
  MOCK_METHOD(CommonFilterStatus, decodeCommonFrame, (RequestCommonFrame&), (override));

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks), (override));
  MOCK_METHOD(HeaderFilterStatus, encodeHeaderFrame, (ResponseHeaderFrame&), (override));
  MOCK_METHOD(CommonFilterStatus, encodeCommonFrame, (ResponseCommonFrame&), (override));

  DecoderFilterCallback* decoder_callbacks_{};
  EncoderFilterCallback* encoder_callbacks_{};
};

class MockStreamFilterConfig : public NamedFilterConfigFactory {
public:
  MockStreamFilterConfig();

  MOCK_METHOD(FilterFactoryCb, createFilterFactoryFromProto,
              (const Protobuf::Message& config, const std::string& stat_prefix,
               Server::Configuration::FactoryContext& context),
              (override));
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, (), (override));
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyRouteConfigProto, (), (override));
  MOCK_METHOD(RouteSpecificFilterConfigConstSharedPtr, createRouteSpecificFilterConfig,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
               ProtobufMessage::ValidationVisitor&),
              (override));
  MOCK_METHOD(std::string, name, (), (const, override));
  MOCK_METHOD(absl::Status, validateCodec, (const TypedExtensionConfig&), (override));
  MOCK_METHOD(std::set<std::string>, configTypes, (), (override));
  MOCK_METHOD(bool, isTerminalFilter, (), (override));
};

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks() = default;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr filter), (override));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr filter), (override));
  MOCK_METHOD(void, addFilter, (StreamFilterSharedPtr filter), (override));
  MOCK_METHOD(absl::string_view, filterConfigName, (), (const, override));
  MOCK_METHOD(void, setFilterConfigName, (absl::string_view name), (override));
};

template <class Base> class MockStreamFilterCallbacks : public Base {
public:
  MOCK_METHOD(Envoy::Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(const CodecFactory&, codecFactory, (), (override));
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const, override));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(Tracing::Span&, activeSpan, (), (override));
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const, override));
  MOCK_METHOD(const Network::Connection*, connection, (), (const, override));
};

class MockDecoderFilterCallback : public MockStreamFilterCallbacks<DecoderFilterCallback> {
public:
  MockDecoderFilterCallback();

  MOCK_METHOD(void, sendLocalReply, (Status, absl::string_view, ResponseUpdateFunction),
              (override));
  MOCK_METHOD(void, continueDecoding, (), (override));
  MOCK_METHOD(void, onResponseHeaderFrame, (ResponseHeaderFramePtr), (override));
  MOCK_METHOD(void, onResponseCommonFrame, (ResponseCommonFramePtr), (override));
  MOCK_METHOD(void, setRequestFramesHandler, (RequestFramesHandler*), (override));
  MOCK_METHOD(void, completeDirectly, (), (override));
  MOCK_METHOD(absl::string_view, filterConfigName, (), (const, override));
};

class MockEncoderFilterCallback : public MockStreamFilterCallbacks<EncoderFilterCallback> {
public:
  MOCK_METHOD(void, continueEncoding, (), (override));
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
