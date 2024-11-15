#pragma once

#include <memory>

#include "envoy/rds/config_traits.h"

namespace Envoy {
namespace Rds {
namespace Common {

/**
 * Basic implementation of ConfigTraits interface. It can be used in simple protocols where no
 * other parameters are required.
 */
template <class RouteConfiguration, class ConfigImpl, class NullConfigImpl>
class ConfigTraitsImpl : public ConfigTraits {
public:
  ConfigConstSharedPtr createNullConfig() const override {
    return std::make_shared<const NullConfigImpl>();
  }

  ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                    Server::Configuration::ServerFactoryContext& context,
                                    bool validate_clusters_default) const override {
    ASSERT(dynamic_cast<const RouteConfiguration*>(&rc));
    return std::make_shared<const ConfigImpl>(static_cast<const RouteConfiguration&>(rc), context,
                                              validate_clusters_default);
  }
};

} // namespace Common
} // namespace Rds
} // namespace Envoy
