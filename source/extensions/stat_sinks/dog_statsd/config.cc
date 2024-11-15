#include "source/extensions/stat_sinks/dog_statsd/config.h"

#include <memory>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/config/metrics/v3/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/network/resolver_impl.h"
#include "source/extensions/stat_sinks/common/statsd/statsd.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DogStatsd {

Stats::SinkPtr
DogStatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                      Server::Configuration::ServerFactoryContext& server) {
  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v3::DogStatsdSink&>(
          config, server.messageValidationContext().staticValidationVisitor());
  auto address_or_error = Network::Address::resolveProtoAddress(sink_config.address());
  THROW_IF_NOT_OK_REF(address_or_error.status());
  Network::Address::InstanceConstSharedPtr address = address_or_error.value();
  ENVOY_LOG(debug, "dog_statsd UDP ip address: {}", address->asString());
  absl::optional<uint64_t> max_bytes;
  if (sink_config.has_max_bytes_per_datagram()) {
    max_bytes = sink_config.max_bytes_per_datagram().value();
  }
  return std::make_unique<Common::Statsd::UdpStatsdSink>(server.threadLocal(), std::move(address),
                                                         true, sink_config.prefix(), max_bytes);
}

ProtobufTypes::MessagePtr DogStatsdSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::metrics::v3::DogStatsdSink>();
}

std::string DogStatsdSinkFactory::name() const { return DogStatsdName; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(DogStatsdSinkFactory, Server::Configuration::StatsSinkFactory,
                        "envoy.dog_statsd");

} // namespace DogStatsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
