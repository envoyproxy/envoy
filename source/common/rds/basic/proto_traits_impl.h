#pragma once

#include <memory>

#include "envoy/rds/config_traits.h"

#include "source/common/config/resource_name.h"

namespace Envoy {
namespace Rds {
namespace Basic {

template <class RouteConfiguration, int NameFieldNumber>
class ProtoTraitsImpl : public ProtoTraits {
public:
  ProtoTraitsImpl() : resource_type_(Envoy::Config::getResourceName<RouteConfiguration>()) {}

  const std::string& resourceType() const override { return resource_type_; };

  int resourceNameFieldNumber() const override { return NameFieldNumber; }

  ProtobufTypes::MessagePtr createEmptyProto() const override {
    return std::make_unique<RouteConfiguration>();
  }

private:
  const std::string resource_type_;
};

} // namespace Basic
} // namespace Rds
} // namespace Envoy
