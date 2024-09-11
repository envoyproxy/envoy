#include "source/extensions/stat_sinks/graphite_statsd/config.h"

#include <memory>

#include "envoy/extensions/stat_sinks/graphite_statsd/v3/graphite_statsd.pb.h"
#include "envoy/extensions/stat_sinks/graphite_statsd/v3/graphite_statsd.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/network/resolver_impl.h"
#include "source/extensions/stat_sinks/common/statsd/statsd.h"
#include "source/extensions/stat_sinks/common/statsd/tag_formats.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace GraphiteStatsd {

Stats::SinkPtr
GraphiteStatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                           Server::Configuration::ServerFactoryContext& server) {

  const auto& statsd_sink = MessageUtil::downcastAndValidate<
      const envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink&>(
      config, server.messageValidationContext().staticValidationVisitor());
  switch (statsd_sink.statsd_specifier_case()) {
  case envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink::StatsdSpecifierCase::
      kAddress: {
    auto address_or_error = Network::Address::resolveProtoAddress(statsd_sink.address());
    THROW_IF_NOT_OK_REF(address_or_error.status());
    Network::Address::InstanceConstSharedPtr address = address_or_error.value();
    ENVOY_LOG(debug, "statsd UDP ip address: {}", address->asString());
    absl::optional<uint64_t> max_bytes;
    if (statsd_sink.has_max_bytes_per_datagram()) {
      max_bytes = statsd_sink.max_bytes_per_datagram().value();
    }
    return std::make_unique<Common::Statsd::UdpStatsdSink>(server.threadLocal(), std::move(address),
                                                           true, statsd_sink.prefix(), max_bytes,
                                                           Common::Statsd::getGraphiteTagFormat());
  }
  case envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink::StatsdSpecifierCase::
      STATSD_SPECIFIER_NOT_SET:
    break;
  }
  PANIC("unexpected statsd specifier enum");
}

ProtobufTypes::MessagePtr GraphiteStatsdSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink>();
}

std::string GraphiteStatsdSinkFactory::name() const { return "envoy.stat_sinks.graphite_statsd"; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(GraphiteStatsdSinkFactory, Server::Configuration::StatsSinkFactory,
                        "envoy.graphite_statsd");

} // namespace GraphiteStatsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
