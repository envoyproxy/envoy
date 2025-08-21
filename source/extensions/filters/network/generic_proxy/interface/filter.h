#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/network/generic_proxy/filter_callbacks.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

// The status of the filter chain.
// 1. Continue: If Continue is returned, the filter chain will continue to the next filter in the
//    chain.
// 2. StopIteration: If StopIteration is returned, the filter chain will stop and not continue
//    to the next filter in the chain until the continueDecoding/continueEncoding is called.
enum class HeaderFilterStatus { Continue, StopIteration };

// The status of the filter chain.
// 1. Continue: If Continue is returned, the filter chain will continue to the next filter in the
//    chain.
// 2. StopIteration: If StopIteration is returned, the filter chain will stop and not continue
//    to the next filter in the chain until the continueDecoding/continueEncoding is called.
enum class CommonFilterStatus { Continue, StopIteration };

class DecoderFilter {
public:
  virtual ~DecoderFilter() = default;

  virtual void onDestroy() PURE;

  virtual void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) PURE;

  virtual HeaderFilterStatus decodeHeaderFrame(RequestHeaderFrame& request) PURE;
  virtual CommonFilterStatus decodeCommonFrame(RequestCommonFrame& request) PURE;
};

class EncoderFilter {
public:
  virtual ~EncoderFilter() = default;

  virtual void onDestroy() PURE;

  virtual void setEncoderFilterCallbacks(EncoderFilterCallback& callbacks) PURE;

  virtual HeaderFilterStatus encodeHeaderFrame(ResponseHeaderFrame& response) PURE;
  virtual CommonFilterStatus encodeCommonFrame(ResponseCommonFrame& response) PURE;
};

class StreamFilter : public DecoderFilter, public EncoderFilter {};

using DecoderFilterSharedPtr = std::shared_ptr<DecoderFilter>;
using EncoderFilterSharedPtr = std::shared_ptr<EncoderFilter>;
using StreamFilterSharedPtr = std::shared_ptr<StreamFilter>;

using TypedExtensionConfig = envoy::config::core::v3::TypedExtensionConfig;

/**
 * Implemented by each generic filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedFilterConfigFactory : public Config::TypedFactory {
public:
  virtual FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty route config proto message route specific
   * config.
   */
  virtual ProtobufTypes::MessagePtr createEmptyRouteConfigProto() PURE;

  /**
   * @return RouteSpecificFilterConfigConstSharedPtr allow the filter to pre-process per route
   * config. Returned object will be stored in the loaded route configuration.
   */
  virtual RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) PURE;

  std::string category() const override { return "envoy.generic_proxy.filters"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() PURE;

  /**
   * @return absl::Status validate the codec config to see if it is compatible with the filter.
   * If the codec config is not compatible with this filter, return an error status.
   */
  virtual absl::Status validateCodec(const TypedExtensionConfig& /*config*/) {
    return absl::OkStatus();
  }

  std::set<std::string> configTypes() override {
    auto config_types = TypedFactory::configTypes();

    if (auto message = createEmptyRouteConfigProto(); message != nullptr) {
      config_types.insert(createReflectableMessage(*message)->GetDescriptor()->full_name());
    }

    return config_types;
  }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
