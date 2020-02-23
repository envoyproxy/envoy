#include "extensions/stat_sinks/dog_statsd/config.h"

#include <memory>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/config/metrics/v3/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/network/resolver_impl.h"

#include "extensions/stat_sinks/common/statsd/statsd.h"
#include "extensions/stat_sinks/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DogStatsd {

Stats::SinkPtr DogStatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                     Server::Instance& server) {
  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v3::DogStatsdSink&>(
          config, server.messageValidationContext().staticValidationVisitor());
  Network::Address::InstanceConstSharedPtr address =
      Network::Address::resolveProtoAddress(sink_config.address());
  ENVOY_LOG(debug, "dog_statsd UDP ip address: {}", address->asString());
  return std::make_unique<Common::Statsd::UdpStatsdSink>(server.threadLocal(), std::move(address),
                                                         true, sink_config.prefix());
}

ProtobufTypes::MessagePtr DogStatsdSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::metrics::v3::DogStatsdSink>();
}

std::string DogStatsdSinkFactory::name() const { return StatsSinkNames::get().DogStatsd; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DogStatsdSinkFactory, Server::Configuration::StatsSinkFactory){"envoy.dog_statsd"};

} // namespace DogStatsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
