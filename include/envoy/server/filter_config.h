#pragma once

#include <functional>

#include "envoy/config/typed_config.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/network/filter.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/factory_context.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Common interface for listener filters and UDP listener filters
 */
class ListenerFilterConfigFactoryBase : public Config::TypedFactory {
public:
  ~ListenerFilterConfigFactoryBase() override = default;
};

/**
 * Implemented by each listener filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedListenerFilterConfigFactory : public ListenerFilterConfigFactoryBase {
public:
  ~NamedListenerFilterConfigFactory() override = default;

  /**
   * Create a particular listener filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error or a Json::Exception if the json configuration is erroneous. The returned
   * callback should always be initialized.
   * @param config supplies the general protobuf configuration for the filter.
   * @param listener_filter_matcher supplies the matcher to decide when filter is enabled.
   * @param context supplies the filter's context.
   * @return Network::ListenerFilterFactoryCb the factory creation function.
   */
  virtual Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& config,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.listener"; }
};

/**
 * Implemented by each UDP listener filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedUdpListenerFilterConfigFactory : public ListenerFilterConfigFactoryBase {
public:
  ~NamedUdpListenerFilterConfigFactory() override = default;

  /**
   * Create a particular UDP listener filter factory implementation. If the implementation is unable
   * to produce a factory with the provided parameters, it should throw an EnvoyException.
   * The returned callback should always be initialized.
   * @param config supplies the general protobuf configuration for the filter
   * @param context supplies the filter's context.
   * @return Network::UdpListenerFilterFactoryCb the factory creation function.
   */
  virtual Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.udp_listener"; }
};

/**
 * Implemented by filter factories that require more options to process the protocol used by the
 * upstream cluster.
 */
class ProtocolOptionsFactory : public Config::TypedFactory {
public:
  ~ProtocolOptionsFactory() override = default;

  /**
   * Create a particular filter's protocol specific options implementation. If the factory
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException.
   * @param config supplies the protobuf configuration for the filter
   * @param validation_visitor message validation visitor instance.
   * @return Upstream::ProtocolOptionsConfigConstSharedPtr the protocol options
   */
  virtual Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& config,
                              ProtobufMessage::ValidationVisitor& validation_visitor) {
    UNREFERENCED_PARAMETER(config);
    UNREFERENCED_PARAMETER(validation_visitor);
    return nullptr;
  }

  /**
   * @return ProtobufTypes::MessagePtr a newly created empty protocol specific options message or
   *         nullptr if protocol specific options are not available.
   */
  virtual ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() { return nullptr; }
};

/**
 * Implemented by each network filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedNetworkFilterConfigFactory : public ProtocolOptionsFactory {
public:
  ~NamedNetworkFilterConfigFactory() override = default;

  /**
   * Create a particular network filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException. The returned
   * callback should always be initialized.
   * @param config supplies the general json configuration for the filter
   * @param filter_chain_factory_context supplies the filter's context.
   * @return Network::FilterFactoryCb the factory creation function.
   */
  virtual Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               FactoryContext& filter_chain_factory_context) PURE;

  std::string category() const override { return "envoy.filters.network"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() { return false; }
};

/**
 * Implemented by each upstream cluster network filter and registered via
 * Registry::registerFactory() or the convenience class RegisterFactory.
 */
class NamedUpstreamNetworkFilterConfigFactory : public ProtocolOptionsFactory {
public:
  ~NamedUpstreamNetworkFilterConfigFactory() override = default;

  /**
   * Create a particular upstream network filter factory implementation. If the implementation is
   * unable to produce a factory with the provided parameters, it should throw an EnvoyException in
   * the case of general error. The returned callback should always be initialized.
   */
  virtual Network::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                                CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.upstream_network"; }
};

/**
 * Implemented by each HTTP filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedHttpFilterConfigFactory : public ProtocolOptionsFactory {
public:
  ~NamedHttpFilterConfigFactory() override = default;

  /**
   * Create a particular http filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException. The returned
   * callback should always be initialized.
   * @param config supplies the general Protobuf message to be marshaled into a filter-specific
   * configuration.
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's context.
   * @return Http::FilterFactoryCb the factory creation function.
   */
  virtual Http::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                             const std::string& stat_prefix,
                                                             FactoryContext& context) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create an empty virtual host, route, or weighted
   *         cluster-local config proto message for v2. The filter config, which arrives in an
   *         opaque message, will be parsed into this empty proto. By default, this method
   *         returns the same value as createEmptyConfigProto, and can be optionally overridden
   *         in implementations.
   */
  virtual ProtobufTypes::MessagePtr createEmptyRouteConfigProto() {
    return createEmptyConfigProto();
  }

  /**
   * @return RouteSpecificFilterConfigConstSharedPtr allow the filter to pre-process per route
   * config. Returned object will be stored in the loaded route configuration.
   */
  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&, ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) {
    return nullptr;
  }

  std::string category() const override { return "envoy.filters.http"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilter() { return false; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
