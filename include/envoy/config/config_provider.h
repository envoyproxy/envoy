#pragma once

#include <memory>

#include "envoy/common/time.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

/**
 * A provider for configuration obtained statically (via static resources in the bootstrap config),
 * inline with a higher level resource or dynamically via xDS APIs.
 *
 * The ConfigProvider is an abstraction layer which higher level components such as the
 * HttpConnectionManager, Listener, etc can leverage to interface with Envoy's configuration
 * mechanisms. Implementations of this interface build upon lower level abstractions such as
 * Envoy::Config::Subscription and Envoy::Config::SubscriptionCallbacks.
 *
 * The interface exposed below allows xDS providers to share the underlying config protos and
 * resulting config implementations (i.e., the ConfigProvider::Config); this enables linear memory
 * scaling based on the size of the configuration set, regardless of the number of threads/workers.
 *
 * Use config() to obtain a shared_ptr to the implementation of the config, and configProtoInfo() to
 * obtain a reference to the underlying config proto and version (applicable only to dynamic config
 * providers).
 */
class ConfigProvider {
public:
  /**
   * The "implementation" of the configuration.
   * Use config() to obtain a typed object that corresponds to the specific configuration
   * represented by this abstract type.
   */
  class Config {
  public:
    virtual ~Config() = default;
  };
  using ConfigConstSharedPtr = std::shared_ptr<const Config>;

  /**
   * The type of API represented by a ConfigProvider.
   */
  enum class ApiType {
    /**
     * A "Full" API delivers a complete configuration as part of each resource (top level
     * config proto); i.e., each resource contains the whole representation of the config intent. An
     * example of this type of API is RDS.
     */
    Full,
    /**
     * A "Delta" API delivers a subset of the config intent as part of each resource (top level
     * config proto). Examples of this type of API are CDS, LDS and SRDS.
     */
    Delta
  };

  /**
   * Stores the config proto as well as the associated version.
   */
  template <typename P> struct ConfigProtoInfo {
    const P& config_proto_;

    // Only populated by dynamic config providers.
    std::string version_;
  };

  using ConfigProtoVector = std::vector<const Protobuf::Message*>;
  /**
   * Stores the config protos associated with a "Delta" API.
   */
  template <typename P> struct ConfigProtoInfoVector {
    const std::vector<const P*> config_protos_;

    // Only populated by dynamic config providers.
    std::string version_;
  };

  virtual ~ConfigProvider() = default;

  /**
   * The type of API.
   */
  virtual ApiType apiType() const PURE;

  /**
   * Returns a ConfigProtoInfo associated with a ApiType::Full provider.
   * @return absl::optional<ConfigProtoInfo<P>> an optional ConfigProtoInfo; the value is set when a
   * config is available.
   */
  template <typename P> absl::optional<ConfigProtoInfo<P>> configProtoInfo() const {
    static_assert(std::is_base_of<Protobuf::Message, P>::value,
                  "Proto type must derive from Protobuf::Message");

    const auto* config_proto = dynamic_cast<const P*>(getConfigProto());
    if (config_proto == nullptr) {
      return absl::nullopt;
    }
    return ConfigProtoInfo<P>{*config_proto, getConfigVersion()};
  }

  /**
   * Returns a ConfigProtoInfoVector associated with a ApiType::Delta provider.
   * @return absl::optional<ConfigProtoInfoVector> an optional ConfigProtoInfoVector; the value is
   * set when a config is available.
   */
  template <typename P> absl::optional<ConfigProtoInfoVector<P>> configProtoInfoVector() const {
    static_assert(std::is_base_of<Protobuf::Message, P>::value,
                  "Proto type must derive from Protobuf::Message");

    const ConfigProtoVector config_protos = getConfigProtos();
    if (config_protos.empty()) {
      return absl::nullopt;
    }
    std::vector<const P*> ret_protos;
    ret_protos.reserve(config_protos.size());
    for (const auto* elem : config_protos) {
      ret_protos.push_back(static_cast<const P*>(elem));
    }
    return ConfigProtoInfoVector<P>{std::move(ret_protos), getConfigVersion()};
  }

  /**
   * Returns the Config corresponding to the provider.
   * @return std::shared_ptr<const C> a shared pointer to the Config.
   */
  template <typename C> std::shared_ptr<const C> config() const {
    static_assert(std::is_base_of<Config, C>::value,
                  "Config type must derive from ConfigProvider::Config");

    return std::dynamic_pointer_cast<const C>(getConfig());
  }

  /**
   * Returns the timestamp associated with the last update to the Config.
   * @return SystemTime the timestamp corresponding to the last config update.
   */
  virtual SystemTime lastUpdated() const PURE;

protected:
  /**
   * Returns the config proto associated with the provider.
   * @return Protobuf::Message* the config proto corresponding to the Config instantiated by the
   *         provider.
   */
  virtual const Protobuf::Message* getConfigProto() const { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  /**
   * Returns the config protos associated with the provider.
   * @return const ConfigProtoVector the config protos corresponding to the Config instantiated by
   *         the provider.
   */
  virtual ConfigProtoVector getConfigProtos() const { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  /**
   * Returns the config version associated with the provider.
   * @return std::string the config version.
   */
  virtual std::string getConfigVersion() const { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  /**
   * Returns the config implementation associated with the provider.
   * @return ConfigConstSharedPtr the config as the base type.
   */
  virtual ConfigConstSharedPtr getConfig() const PURE;
};

using ConfigProviderPtr = std::unique_ptr<ConfigProvider>;

} // namespace Config
} // namespace Envoy
