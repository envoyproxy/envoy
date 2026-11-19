#pragma once

#include "envoy/rds/config_traits.h"

#include "source/common/config/resource_name.h"

namespace Envoy {
namespace Rds {
namespace Common {

/**
 * Basic implementation of ProtoTraits interface. It can be used in all protocols.
 */
template <class RouteConfiguration, int NameFieldNumber>
class ProtoTraitsImpl : public ProtoTraits {
public:
  ProtoTraitsImpl() : resource_type_(Envoy::Config::getResourceName<RouteConfiguration>()) {}

  const std::string& resourceType() const override { return resource_type_; };

  int resourceNameFieldNumber() const override { return NameFieldNumber; }

private:
  const std::string resource_type_;
};

} // namespace Common
} // namespace Rds
} // namespace Envoy
