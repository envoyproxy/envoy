#pragma once

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "absl/container/flat_hash_set.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

/*
 * An abstract class that represents an xDS source. The toKey() function provides a unique
 * identifier for the source for a group of xDS resources in a DiscoveryResponse.
 */
class XdsSourceId {
public:
  virtual ~XdsSourceId() = default;

  /**
   * Returns a unique string representation of the source. The string can be used as an identifier
   * for the source (e.g. as a key in a hash map).
   * @return string representation of the source.
   */
  virtual std::string toKey() const PURE;
};

/**
 * An interface for hooking into xDS resource fetch and update events.
 * Currently, this interface only supports the SotW (state-of-the-world) xDS protocol.
 *
 * Instances of this interface get invoked on the main Envoy thread. Thus, it is important for
 * implementations of this interface to not execute any blocking operations on the same thread.
 * Any blocking operations (e.g. flushing config to disk) should be handed off to a separate thread
 * so we don't block the main thread.
 */
class XdsResourcesDelegate {
public:
  virtual ~XdsResourcesDelegate() = default;

  /**
   * Returns a list of xDS resources for the given source and names. The implementation is not
   * required to return all (or any) of the requested resources, but the resources it does return
   * are required to be in the set of requested resources. An empty resource name set will return
   * all known resources from the given xDS source (i.e. the "wildcard").
   *
   * This function is intended to only be called on xDS fetch startup, and allows the
   * implementation to return a set of resources to be loaded and used by the Envoy instance
   * (e.g. persisted resources in local storage).
   *
   * @param source_id The xDS source for the requested resources.
   * @param resource_names The names of the requested resources.
   * @return A list of xDS resources for the given source.
   */
  virtual std::vector<envoy::service::discovery::v3::Resource>
  getResources(const XdsSourceId& source_id,
               const absl::flat_hash_set<std::string>& resource_names) const PURE;

  /**
   * Invoked when SotW xDS configuration updates have been received from an xDS authority, have been
   * applied on the Envoy instance, and are about to be ACK'ed.
   *
   * @param source_id The xDS source for the requested resources.
   * @param resources The resources for the given source received on the DiscoveryResponse.
   */
  virtual void onConfigUpdated(const XdsSourceId& source_id,
                               const std::vector<DecodedResourceRef>& resources) PURE;

  /**
   * Invoked when loading a resource obtained from the getResources() call resulted in a failure.
   * This would typically happen when there is a parsing or validation error on the xDS resource
   * protocol buffer.
   *
   * @param source_id The xDS source for the requested resource.
   * @param resource_name The name of the resource.
   * @param exception The exception that occurred, if any.
   */
  virtual void onResourceLoadFailed(const XdsSourceId& source_id, const std::string& resource_name,
                                    const absl::optional<EnvoyException>& exception) PURE;
};

using XdsResourcesDelegatePtr = std::unique_ptr<XdsResourcesDelegate>;
using XdsResourcesDelegateOptRef = OptRef<XdsResourcesDelegate>;

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
   * @param dispatcher The dispatcher for the thread.
   * @return The created XdsResourcesDelegate instance
   */
  virtual XdsResourcesDelegatePtr
  createXdsResourcesDelegate(const ProtobufWkt::Any& config,
                             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                             Event::Dispatcher& dispatcher) PURE;

  std::string category() const override { return "envoy.xds_delegates"; }
};

} // namespace Config
} // namespace Envoy
