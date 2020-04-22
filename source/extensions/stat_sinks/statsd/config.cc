#include "extensions/stat_sinks/statsd/config.h"

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
namespace Statsd {

Stats::SinkPtr StatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                  Server::Instance& server) {

  const auto& statsd_sink =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v3::StatsdSink&>(
          config, server.messageValidationContext().staticValidationVisitor());
  switch (statsd_sink.statsd_specifier_case()) {
  case envoy::config::metrics::v3::StatsdSink::StatsdSpecifierCase::kAddress: {
    Network::Address::InstanceConstSharedPtr address =
        Network::Address::resolveProtoAddress(statsd_sink.address());
    ENVOY_LOG(debug, "statsd UDP ip address: {}", address->asString());
    return std::make_unique<Common::Statsd::UdpStatsdSink>(server.threadLocal(), std::move(address),
                                                           false, statsd_sink.prefix());
  }
  case envoy::config::metrics::v3::StatsdSink::StatsdSpecifierCase::kTcpClusterName:
    ENVOY_LOG(debug, "statsd TCP cluster: {}", statsd_sink.tcp_cluster_name());
    return std::make_unique<Common::Statsd::TcpStatsdSink>(
        server.localInfo(), statsd_sink.tcp_cluster_name(), server.threadLocal(),
        server.clusterManager(), server.stats(), statsd_sink.prefix());
  default:
    // Verified by schema.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

ProtobufTypes::MessagePtr StatsdSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::metrics::v3::StatsdSink>();
}

std::string StatsdSinkFactory::name() const { return StatsSinkNames::get().Statsd; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
REGISTER_FACTORY(StatsdSinkFactory, Server::Configuration::StatsSinkFactory){"envoy.statsd"};

} // namespace Statsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
