#pragma once

#include <functional>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"
#include "envoy/http/filter.h"
#include "envoy/init/manager.h"
#include "envoy/network/filter.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/protobuf/protobuf.h"

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
 * Implemented by each UDP session filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class NamedUdpSessionFilterConfigFactory : public Envoy::Config::TypedFactory {
public:
  ~NamedUdpSessionFilterConfigFactory() override = default;

  /**
   * Create a particular UDP session filter factory implementation. If the implementation is
   * unable to produce a factory with the provided parameters, it should throw an EnvoyException
   * in the case of general error. The returned callback should always be initialized.
   * @param config supplies the configuration for the filter
   * @param context supplies the filter's context.
   * @return UdpSessionFilterFactoryCb the factory creation function.
   */
  virtual Network::UdpSessionFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.udp.session"; }
};

/**
 * Implemented by each QUIC listener filter and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 */
class NamedQuicListenerFilterConfigFactory : public ListenerFilterConfigFactoryBase {
public:
  ~NamedQuicListenerFilterConfigFactory() override = default;

  /**
   * Create a particular listener filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException in the case
   * of general error or a Json::Exception if the json configuration is erroneous. The returned
   * callback should always be initialized.
   * @param config supplies the general protobuf configuration for the filter.
   * @param listener_filter_matcher supplies the matcher to decide when filter is enabled.
   * @param context supplies the filter's context.
   * @return Network::QuicListenerFilterFactoryCb the factory creation function.
   */
  virtual Network::QuicListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& config,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.quic_listener"; }
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
   * or an error message.
   */
  virtual absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr>
  createProtocolOptionsConfig(const Protobuf::Message& config,
                              ProtocolOptionsFactoryContext& factory_context) {
    UNREFERENCED_PARAMETER(config);
    UNREFERENCED_PARAMETER(factory_context);
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
   * @return Network::FilterFactoryCb the factory creation function or an error status.
   */
  virtual absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               FactoryContext& filter_chain_factory_context) PURE;

  std::string category() const override { return "envoy.filters.network"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilterByProto(const Protobuf::Message&, ServerFactoryContext&) {
    return false;
  }
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
  virtual Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               UpstreamFactoryContext& context) PURE;

  std::string category() const override { return "envoy.filters.upstream_network"; }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilterByProto(const Protobuf::Message&, ServerFactoryContext&) {
    return false;
  }
};

using FilterDependenciesPtr =
    std::unique_ptr<envoy::extensions::filters::common::dependency::v3::FilterDependencies>;
using MatchingRequirementsPtr =
    std::unique_ptr<envoy::extensions::filters::common::dependency::v3::MatchingRequirements>;

/**
 * Implemented by each HTTP filter and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class HttpFilterConfigFactoryBase : public ProtocolOptionsFactory {
public:
  ~HttpFilterConfigFactoryBase() override = default;

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
   * @return FilterDependenciesPtr specification of dependencies required or
   * provided on the decode and encode paths. This function returns an empty
   * filter dependencies specification by default, and can be overridden.
   */
  virtual FilterDependenciesPtr dependencies() {
    return std::make_unique<
        envoy::extensions::filters::common::dependency::v3::FilterDependencies>();
  }

  /**
   * Match requirements for the filters created by this filter factory. These requirements inform
   * the validator what input/outputs are valid for a match tree specified via the
   * ExtensionWithMatcher wrapper, allowing us to reject the match tree at configuration time if
   * there are any violations.
   *
   * @return MatchingRequirementsPtr specification of matching requirements
   * for a match tree that can be used with this filter factory.
   */
  virtual MatchingRequirementsPtr matchingRequirements() {
    return std::make_unique<
        envoy::extensions::filters::common::dependency::v3::MatchingRequirements>();
  }

  std::set<std::string> configTypes() override {
    auto config_types = TypedFactory::configTypes();

    if (auto message = createEmptyRouteConfigProto(); message != nullptr) {
      config_types.insert(createReflectableMessage(*message)->GetDescriptor()->full_name());
    }

    return config_types;
  }

  /**
   * @return bool true if this filter must be the last filter in a filter chain, false otherwise.
   */
  virtual bool isTerminalFilterByProto(const Protobuf::Message&,
                                       Server::Configuration::ServerFactoryContext&) {
    return false;
  }
};

class NamedHttpFilterConfigFactory : public virtual HttpFilterConfigFactoryBase {
public:
  /**
   * Create a particular http filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters, it should throw an EnvoyException. The returned
   * callback should always be initialized.
   * @param config supplies the general Protobuf message to be marshaled into a filter-specific
   * configuration.
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's context.
   * @return  absl::StatusOr<Http::FilterFactoryCb> the factory creation function or an error if
   * creation fails.
   */
  virtual absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) PURE;

  /**
   * Create a particular http filter factory implementation. If the implementation is unable to
   * produce a factory with the provided parameters or this method is not supported, it should throw
   * an EnvoyException. The returned callback should always be initialized.
   * @param config supplies the general Protobuf message to be marshaled into a filter-specific
   * configuration.
   * @param stat_prefix prefix for stat logging
   * @param context supplies the filter's ServerFactoryContext.
   * @return Http::FilterFactoryCb the factory creation function.
   */
  virtual Http::FilterFactoryCb
  createFilterFactoryFromProtoWithServerContext(const Protobuf::Message&, const std::string&,
                                                Server::Configuration::ServerFactoryContext&) {
    ExceptionUtil::throwEnvoyException(
        "Creating filter factory from server factory context is not supported");
    return nullptr;
  }
};

class UpstreamHttpFilterConfigFactory : public virtual HttpFilterConfigFactoryBase {
public:
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
  virtual absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stat_prefix,
                               Server::Configuration::UpstreamFactoryContext& context) PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
