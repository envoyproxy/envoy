#include "server/config/stats/statsd.h"

#include <string>

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/config/metrics/v2/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/resolver_impl.h"
#include "common/stats/statsd.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr StatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                  Server::Instance& server) {

  const auto& statsd_sink =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v2::StatsdSink&>(config);
  switch (statsd_sink.statsd_specifier_case()) {
  case envoy::config::metrics::v2::StatsdSink::kAddress: {
    Network::Address::InstanceConstSharedPtr address =
        Network::Address::resolveProtoAddress(statsd_sink.address());
    ENVOY_LOG(debug, "statsd UDP ip address: {}", address->asString());
    return Stats::SinkPtr(
        new Stats::Statsd::UdpStatsdSink(server.threadLocal(), std::move(address), false));
    break;
  }
  case envoy::config::metrics::v2::StatsdSink::kTcpClusterName:
    ENVOY_LOG(debug, "statsd TCP cluster: {}", statsd_sink.tcp_cluster_name());
    return Stats::SinkPtr(new Stats::Statsd::TcpStatsdSink(
        server.localInfo(), statsd_sink.tcp_cluster_name(), server.threadLocal(),
        server.clusterManager(), server.stats()));
    break;
  default:
    // Verified by schema.
    NOT_REACHED;
  }
}

ProtobufTypes::MessagePtr StatsdSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::config::metrics::v2::StatsdSink>(
      new envoy::config::metrics::v2::StatsdSink());
}

std::string StatsdSinkFactory::name() { return Config::StatsSinkNames::get().STATSD; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<StatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
