#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/rds/config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Rds {

/**
 * Traits of the protocol specific route configuration and proto.
 * The generic rds classes will call the methods of this interface
 * to get information which are not visible for them directly.
 */
class ConfigTraits {
public:
  virtual ~ConfigTraits() = default;

  /**
   * Give the full name of the route configuration proto description.
   * For example 'envoy.config.route.v3.RouteConfiguration'
   */
  virtual std::string resourceType() const PURE;

  /**
   * Create a dummy config object without actual route configuration.
   * This object will be used before the first valid route configuration is fetched.
   */
  virtual ConfigConstSharedPtr createConfig() const PURE;

  /**
   * Create an empty route configuration proto object.
   */
  virtual ProtobufTypes::MessagePtr createProto() const PURE;

  /**
   * Runtime check if the provided proto message object is really a route configuration instance.
   * Throw an std::bad_cast exception if not.
   * Every other method below this assumes the proto message is already
   * validated and doesn't do any further runtime check.
   */
  virtual const Protobuf::Message& validateResourceType(const Protobuf::Message& rc) const PURE;

  /**
   * Check if a valid config object can be made based on the provided route configuration proto.
   * Throw an exception if not.
   */
  virtual const Protobuf::Message& validateConfig(const Protobuf::Message& rc) const PURE;

  /**
   * Gives back the name from the route configuration proto.
   * The object behind the returned reference has to have the same lifetime like the proto.
   */
  virtual const std::string& resourceName(const Protobuf::Message& rc) const PURE;

  /**
   * Create a config object based on a route configuration.
   */
  virtual ConfigConstSharedPtr createConfig(const Protobuf::Message& rc) const PURE;

  /**
   * Clones the route configuration proto.
   */
  virtual ProtobufTypes::MessagePtr cloneProto(const Protobuf::Message& rc) const PURE;
};

} // namespace Rds
} // namespace Envoy
