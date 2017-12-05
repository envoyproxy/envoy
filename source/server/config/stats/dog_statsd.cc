#include "server/config/stats/dog_statsd.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/resolver_impl.h"
#include "common/stats/statsd.h"

#include "api/stats.pb.h"
#include "api/stats.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr DogStatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                     Server::Instance& server) {
  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::api::v2::DogStatsdSink&>(config);
  Network::Address::InstanceConstSharedPtr address =
      Network::Address::resolveProtoAddress(sink_config.address());
  ENVOY_LOG(debug, "dog_statsd UDP ip address: {}", address->asString());
  return Stats::SinkPtr(
      new Stats::Statsd::UdpStatsdSink(server.threadLocal(), std::move(address), true));
}

ProtobufTypes::MessagePtr DogStatsdSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::api::v2::DogStatsdSink>(new envoy::api::v2::DogStatsdSink());
}

std::string DogStatsdSinkFactory::name() { return Config::StatsSinkNames::get().DOG_STATSD; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<DogStatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
