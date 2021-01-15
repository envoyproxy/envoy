#pragma once

#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

using ConfigAppliedCb = std::function<void()>;

class ExtensionConfigProviderBase {
public:
  virtual ~ExtensionConfigProviderBase() = default;

  /**
   * Get the extension configuration resource name.
   **/
  virtual const std::string& name() PURE;

  /**
   * Validate that the configuration is applicable in the context of the provider. If an exception
   * is thrown by any of the config providers for an update, the extension configuration update is
   * rejected.
   * @param proto_config is the candidate configuration update.
   */
  virtual void validateConfig(const ProtobufWkt::Any& proto_config) PURE;

  /**
   * Update the provider with a new configuration.
   * @param proto_config is a candidate configuration update.
   * @param version_info is the version of the new extension configuration.
   * @param cb the continuation callback for a completed configuration application.
   */
  virtual void onConfigUpdate(const ProtobufWkt::Any& proto_config, const std::string& version_info,
                              ConfigAppliedCb cb) PURE;
};

/**
 * A provider for extension configurations obtained either statically or via
 * the extension configuration discovery service. Dynamically updated extension
 * configurations may share subscriptions across extension config providers.
 */
template <class FactoryCallback>
class ExtensionConfigProvider : public ExtensionConfigProviderBase {
public:
  virtual ~ExtensionConfigProvider() = default;

  /**
   * @return FactoryCallback an extension factory callback. Note that if the
   * provider has not yet performed an initial configuration load and no
   * default is provided, an empty optional will be returned. The factory
   * callback is the latest version of the extension configuration, and should
   * generally apply only to new requests and connections.
   */
  virtual absl::optional<FactoryCallback> config() PURE;
};

} // namespace Config
} // namespace Envoy
