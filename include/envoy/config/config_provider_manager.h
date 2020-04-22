#pragma once

#include <string>

#include "envoy/config/config_provider.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * A ConfigProvider manager which instantiates static and dynamic (xDS) providers.
 *
 * ConfigProvider objects are owned by the caller of the
 * createXdsConfigProvider()/createStaticConfigProvider() functions. The ConfigProviderManager holds
 * raw pointers to those objects.
 *
 * Configuration implementations returned by ConfigProvider::config() are immutable, which allows
 * them to share the underlying objects such as config protos and subscriptions (for dynamic
 * providers) without synchronization related performance penalties. This enables linear memory
 * growth based on the size of the configuration set, regardless of the number of threads/objects
 * that must hold a reference/pointer to them.
 */
class ConfigProviderManager {
public:
  class OptionalArg {
  public:
    virtual ~OptionalArg() = default;
  };

  class NullOptionalArg : public OptionalArg {
  public:
    NullOptionalArg() = default;
    ~NullOptionalArg() override = default;
  };

  virtual ~ConfigProviderManager() = default;

  /**
   * Returns a dynamic ConfigProvider which receives configuration via an xDS API.
   * A shared ownership model is used, such that the underlying subscription, config proto
   * and Config are shared amongst all providers relying on the same config source.
   * @param config_source_proto supplies the proto containing the xDS API configuration.
   * @param factory_context is the context to use for the provider.
   * @param init_manager is the Init::Manager to use for the provider.
   * @param stat_prefix supplies the prefix to use for statistics.
   * @param optarg supplies an optional argument with data specific to the concrete class.
   * @return ConfigProviderPtr a newly allocated dynamic config provider which shares underlying
   *                           data structures with other dynamic providers configured with the same
   *                           API source.
   */
  virtual ConfigProviderPtr
  createXdsConfigProvider(const Protobuf::Message& config_source_proto,
                          Server::Configuration::ServerFactoryContext& factory_context,
                          Init::Manager& init_manager, const std::string& stat_prefix,
                          const OptionalArg& optarg) PURE;

  /**
   * Returns a ConfigProvider associated with a statically specified configuration.
   * @param config_proto supplies the configuration proto.
   * @param factory_context is the context to use for the provider.
   * @param optarg supplies an optional argument with data specific to the concrete class.
   * @return ConfigProviderPtr a newly allocated static config provider.
   */
  virtual ConfigProviderPtr
  createStaticConfigProvider(const Protobuf::Message& config_proto,
                             Server::Configuration::ServerFactoryContext& factory_context,
                             const OptionalArg& optarg) {
    UNREFERENCED_PARAMETER(config_proto);
    UNREFERENCED_PARAMETER(factory_context);
    UNREFERENCED_PARAMETER(optarg);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  /**
   * Returns a ConfigProvider associated with a statically specified configuration. This is intended
   * to be used when a set of configuration protos is required to build the full configuration.
   * @param config_protos supplies a vector of configuration protos.
   * @param factory_context is the context to use for the provider.
   * @param optarg supplies an optional argument with data specific to the concrete class.
   * @return ConfigProviderPtr a newly allocated static config provider.
   */
  virtual ConfigProviderPtr
  createStaticConfigProvider(ProtobufTypes::ConstMessagePtrVector&& config_protos,
                             Server::Configuration::ServerFactoryContext& factory_context,
                             const OptionalArg& optarg) {
    UNREFERENCED_PARAMETER(config_protos);
    UNREFERENCED_PARAMETER(factory_context);
    UNREFERENCED_PARAMETER(optarg);
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
};

} // namespace Config
} // namespace Envoy
