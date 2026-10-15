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

  // The simple route configurations that use this trait have no resources that need to be
  // warmed up, so the init manager is ignored.
  ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                    Server::Configuration::ServerFactoryContext& context,
                                    Init::Manager&, bool validate_clusters_default) const override {
    ASSERT(Envoy::Protobuf::DynamicCastMessage<RouteConfiguration>(&rc));
    return std::make_shared<const ConfigImpl>(static_cast<const RouteConfiguration&>(rc), context,
                                              validate_clusters_default);
  }
};

} // namespace Common
} // namespace Rds
} // namespace Envoy
