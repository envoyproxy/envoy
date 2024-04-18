#pragma once

#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.validate.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/geoip_providers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

using DriverSharedPtr = Envoy::Geolocation::DriverSharedPtr;

class MaxmindProviderFactory
    : public Common::FactoryBase<envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig> {
public:
  MaxmindProviderFactory();

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig>();
  }

private:
  // FactoryBase
  DriverSharedPtr createGeoipProviderDriverTyped(
      const envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(MaxmindProviderFactory);

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
