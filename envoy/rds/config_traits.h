#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/rds/config.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Rds {

/**
 * Traits of the protocol specific route configuration and proto.
 * The generic rds classes will call the methods of this interface
 * to get information which is not visible for them directly.
 */
class ProtoTraits {
public:
  virtual ~ProtoTraits() = default;

  /**
   * Give the full name of the route configuration proto description.
   * For example 'envoy.config.route.v3.RouteConfiguration'
   */
  virtual const std::string& resourceType() const PURE;

  /**
   * Gives back the name field tag number of the route configuration proto.
   */
  virtual int resourceNameFieldNumber() const PURE;

  /**
   * Create an empty route configuration proto object.
   */
  virtual ProtobufTypes::MessagePtr createEmptyProto() const PURE;
};

class ConfigTraits {
public:
  virtual ~ConfigTraits() = default;

  /**
   * Create a dummy config object without actual route configuration.
   * This object will be used before the first valid route configuration is fetched.
   */
  virtual ConfigConstSharedPtr createNullConfig() const PURE;

  /**
   * Create a config object based on a route configuration.
   * The full name of the type of the parameter message is
   * guaranteed to match with the return value of ProtoTraits::resourceType.
   * Both dynamic or static cast can be applied to downcast the message
   * to the corresponding route configuration class.
   * @param rc supplies the RouteConfiguration.
   * @param context supplies the context of the server factory.
   * @param validate_clusters_default specifies whether the clusters that the route
   *    table refers to will be validated by the cluster manager. Currently thrift
   *    route config provider manager validates the clusters for static route config
   *    by default but doesn't validate the clusters for TRDS.
   * @throw EnvoyException if the new config can't be applied of.
   */
  virtual ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                            Server::Configuration::ServerFactoryContext& context,
                                            bool validate_clusters_default) const PURE;
};

} // namespace Rds
} // namespace Envoy
