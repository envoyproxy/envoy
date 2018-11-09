#pragma once

#include <memory>

#include "envoy/common/time.h"

#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

/**
 * A provider for configuration obtained ether statically or dynamically via xDS APIs.
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
    virtual ~Config() {}
  };
  using ConfigConstSharedPtr = std::shared_ptr<const Config>;

  /**
   * Stores the config proto as well as the associated version.
   */
  template <typename P> struct ConfigInfo {
    const P& config_;

    std::string version_;
  };

  virtual ~ConfigProvider() {}

  /**
   * Returns ConfigInfo associated with the provider.
   * @return absl::optional<ConfigInfo<P>> an optional ConfigInfo; the value is set when a config is
   *         available.
   */
  template <typename P> absl::optional<ConfigInfo<P>> configInfo() {
    static_assert(std::is_base_of<Protobuf::Message, P>::value,
                  "Proto type must derive from Protobuf::Message");

    const auto* config_proto = dynamic_cast<const P*>(getConfigProto());
    if (!config_proto) {
      return {};
    }
    return ConfigInfo<P>{*config_proto, getConfigVersion()};
  }

  /**
   * Returns the Config corresponding to the provider.
   * @return std::shared_ptr<const C> a shared pointer to the Config.
   */
  template <typename C> std::shared_ptr<const C> config() {
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
   * @return Protobuf::Message* the config proto corresponding to the Config contained by the
   *         provider.
   */
  virtual const Protobuf::Message* getConfigProto() const PURE;

  /**
   * Returns the config version associated with dynamically delivered configuration.
   * @return std::string the version associated with the config.
   */
  virtual std::string getConfigVersion() const PURE;

  /**
   * Returns the config implementation associated with the provider.
   * @return ConfigConstSharedPtr the config as the base type.
   */
  virtual ConfigConstSharedPtr getConfig() const PURE;
};

using ConfigProviderPtr = std::unique_ptr<ConfigProvider>;

} // namespace Config
} // namespace Envoy
