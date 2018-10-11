#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/find_and_replace/find_and_replace.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

/**
 * Config registration for the FindAndReplace Filter. @see NamedNetworkFilterConfigFactory.
 */
class ConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactory(const Json::Object&,
                                               Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext&) override {

    ConfigConstSharedPtr filter_config(
        std::make_shared<Config>(dynamic_cast<const Envoy::ProtobufWkt::Struct&>(proto_config)));

    return [filter_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<Filter>(filter_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Struct>();
  }

  std::string name() override { return "find_and_replace"; }
};

/**
 * Static registration for the FindAndReplace filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace FindAndReplace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
