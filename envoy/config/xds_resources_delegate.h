#pragma once

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

namespace Envoy {
namespace Config {

/**
 * An interface for hooking into xDS resource fetch and update events.
 * Currently, this interface only supports the SotW (state-of-the-world) xDS protocol.
 */
class XdsResourcesDelegate {
public:
  virtual ~XdsResourcesDelegate() = default;

  /**
   * Returns a list of xDS resources for the given authority and type.  It is up to the
   * implementation to determine what resources to supply, if any.
   *
   * This function is intended to only be called on xDS fetch startup, and allows the
   * implementation to return a set of resources to be loaded and used by the Envoy instance
   * (e.g. persisted resources in local storage).
   *
   * @param authority_id Unique id for the authority/control plane from which the resources are
   *   obtained. Typically either a cluster name or a host name.
   * @param resource_type_url The URL for the type of the resource (e.g. Secrets, Clusters, etc).
   * @return A set of xDS resources for the given authority and type.
   */
  std::vector<envoy::service::discovery::v3::Resource>
  getResources(const std::string& authority_id, const std::string& resource_type_url);

  /**
   * Invoked when SotW xDS configuration updates have been received from an xDS authority, have been
   * applied on the Envoy instance, and are about to be ACK'ed.
   *
   * @param authority_id Unique id for the authority/control plane from which the resources are
   *   obtained. Typically either a cluster name or a host name.
   * @param resource_type_url The URL for the type of the resource (e.g. Secrets, Clusters, etc).
   * @param resources The resources sent by the authority for the given type.
   */
  virtual void onConfigUpdated(const std::string& authority_id,
                               const std::string& resource_type_url,
                               const std::vector<DecodedResourceRef>& resources) PURE;
};

using XdsResourcesDelegatePtr = std::unique_ptr<XdsResourcesDelegate>;

/**
 * A factory abstract class for creating instances of XdsResourcesDelegate.
 */
class XdsResourcesDelegateFactory : public Config::TypedFactory {
public:
  ~XdsResourcesDelegateFactory() override = default;

  /**
   * Creates a XdsResourcesDelegate using the given configuration.
   * @param config Configuration of the XdsResourcesDelegate to create.
   * @param validation_visitor Validates the configuration.
   * @param api The APIs that can be used by the delegate.
   * @return The created XdsResourcesDelegate instance
   */
  virtual XdsResourcesDelegatePtr
  createXdsResourcesDelegate(const ProtobufWkt::Any& config,
                             ProtobufMessage::ValidationVisitor& validation_visitor,
                             Api::Api& api) PURE;

  std::string category() const override { return "envoy.config.xds"; }
};

} // namespace Config
} // namespace Envoy
