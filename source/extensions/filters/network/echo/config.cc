#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/echo/echo.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class EchoConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object&, Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<EchoFilter>());
    };
  }

  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<EchoFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return NetworkFilterNames::get().ECHO; }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<EchoConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
