#pragma once

#include <memory>

#include "envoy/rds/config_traits.h"

namespace Envoy {
namespace Rds {
namespace Basic {

template <class RouteConfiguration, class ConfigImpl, class NullConfigImpl>
class ConfigTraitsImpl : public ConfigTraits {
public:
  ConfigConstSharedPtr createNullConfig() const override {
    return std::make_shared<const NullConfigImpl>();
  }

  ConfigConstSharedPtr createConfig(const Protobuf::Message& rc) const override {
    ASSERT(dynamic_cast<const RouteConfiguration*>(&rc));
    return std::make_shared<const ConfigImpl>(static_cast<const RouteConfiguration&>(rc));
  }
};

} // namespace Basic
} // namespace Rds
} // namespace Envoy
