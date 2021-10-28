#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/rds/config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Rds {

/**
 * Traits of route configuration and proto.
 */
class ConfigTraits {
public:
  virtual ~ConfigTraits() = default;

  virtual std::string resourceType() const PURE;

  /**
   * Create a dummy config object without actual route configuration.
   * This object will be used before the first valid route configuration is fetched.
   */
  virtual ConfigConstSharedPtr createConfig() const PURE;

  virtual ProtobufTypes::MessagePtr createProto() const PURE;

  virtual const Protobuf::Message& validateResourceType(const Protobuf::Message& rc) const PURE;
  virtual const Protobuf::Message& validateConfig(const Protobuf::Message& rc) const PURE;

  virtual const std::string& resourceName(const Protobuf::Message& rc) const PURE;

  /**
   * Create a config object based on a route configuration.
   */
  virtual ConfigConstSharedPtr createConfig(const Protobuf::Message& rc) const PURE;

  virtual ProtobufTypes::MessagePtr cloneProto(const Protobuf::Message& rc) const PURE;
};

} // namespace Rds
} // namespace Envoy
