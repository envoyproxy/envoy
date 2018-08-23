#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/inner_sni_reader/inner_sni_reader.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace InnerSniReader {

/**
 * Config registration for the inner_sni_reader filter. @see NamedNetworkFilterConfigFactory.
 */
class InnerSniReaderConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object&,
                      Server::Configuration::FactoryContext& context) override {
    // TODO: call createFilterFactoryFromProto and remove duplicate code
    ConfigSharedPtr filter_config(new Config(context.scope()));
    return [filter_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<InnerSniReaderFilter>(filter_config));
    };
  }

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext& context) override {
    ConfigSharedPtr filter_config(new Config(context.scope()));
    return [filter_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<InnerSniReaderFilter>(filter_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return NetworkFilterNames::get().InnerSniReader; }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<InnerSniReaderConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace InnerSniReader
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
