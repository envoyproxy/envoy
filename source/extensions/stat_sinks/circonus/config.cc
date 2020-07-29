#include "extensions/stat_sinks/circonus/config.h"
#include "extensions/stat_sinks/circonus/circonus_stat_sink.h"

#include <memory>

#include "envoy/config/metrics/v3/stats_circonus.pb.h"
#include "envoy/registry/registry.h"

#include "common/network/resolver_impl.h"

#include "extensions/stat_sinks/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Circonus {

Stats::SinkPtr CirconusSinkFactory::createStatsSink(const Protobuf::Message& /* config */,
                                                    Server::Configuration::ServerFactoryContext& server) {
  return std::make_unique<CirconusStatSink>(server);
}

std::string CirconusSinkFactory::name() const { return StatsSinkNames::get().Circonus; }

ProtobufTypes::MessagePtr CirconusSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::metrics::v3::CirconusSink>();
}

REGISTER_FACTORY(CirconusSinkFactory, Server::Configuration::StatsSinkFactory);

} // namespace Circonus
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
