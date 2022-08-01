#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/key_value_store.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

namespace Envoy {
namespace Config {

/**
 * An interface for receiving and acting upon accepted xDS configuration updates for which an ACK
 * is being sent.
 */
class ConfigUpdatedCallback {
public:
  virtual ~ConfigUpdatedCallback() = default;

  /**
   * Invoked when SotW xDS configuration updates have been received from a control plane, have been
   * applied on the Envoy instance, and are about to be ACK'ed.
   * @param control_plane_id Unique id for the control plane from which the resources are obtained.
   *   Typically either a cluster name or a host name.
   * @param resource_type_url The URL for the type of the resource (e.g. Secrets, Clusters, etc).
   * @param resources The resources sent by the control plane for the given type.
   */
  virtual void onConfigUpdated(const std::string& control_plane_id,
                               const std::string& resource_type_url,
                               const std::vector<DecodedResourceRef>& resources) PURE;
};

using ConfigUpdatedCallbackPtr = std::unique_ptr<ConfigUpdatedCallback>;
using ConfigUpdatedCallbackList = std::vector<ConfigUpdatedCallbackPtr>;

/**
 * A factory abstract class for creating instances of ConfigUpdatedCallback.
 */
class ConfigUpdatedCallbackFactory : public Config::TypedFactory {
public:
  ~ConfigUpdatedCallbackFactory() override = default;

  /**
   * Creates a ConfigUpdatedCallback using the given configuration.
   * @param config Configuration of the ConfigUpdatedCallback to create.
   * @param key_value_store Key-value store which holds the persisted xDS configuration.
   * @param validation_visitor Validates the configuration.
   * @return The created ConfigUpdatedCallback instance
   */
  virtual ConfigUpdatedCallbackPtr
  createConfigUpdatedCallback(const ProtobufWkt::Any& config, KeyValueStore* key_value_store,
                              ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.config.update_callbacks"; }
};

} // namespace Config
} // namespace Envoy
