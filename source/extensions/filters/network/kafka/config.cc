#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/network/kafka/kafka_filter.h"
#include "extensions/filters/network/kafka/metrics_holder.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Config registration for the Kafka filter. @see NamedNetworkFilterConfigFactory.
 */
class KafkaConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object&,
                      Server::Configuration::FactoryContext& context) override {
    return createInternal(context.scope());
  }

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext& context) override {
    return createInternal(context.scope());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return NetworkFilterNames::get().Kafka; }

private:
  Network::FilterFactoryCb createInternal(Stats::Scope& scope) {
    std::shared_ptr<MetricsHolder> metrics_holder = std::make_shared<MetricsHolder>(scope);
    return [&scope, metrics_holder](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<KafkaFilter>(scope, metrics_holder));
    };
  }
};

/**
 * Static registration for the Kafka filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<KafkaConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
