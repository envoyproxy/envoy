#include "server/config/stats/hystrix.h"

#include <string>

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/config/metrics/v2/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/network/resolver_impl.h"
#include "common/stats/hystrix.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr HystrixSinkFactory::createStatsSink(const Protobuf::Message&,
                                                   Server::Instance& server) {

  return Stats::SinkPtr(new Stats::HystrixNameSpace::HystrixSink(server));
}

ProtobufTypes::MessagePtr HystrixSinkFactory::createEmptyConfigProto() {
  // TDOD (@trabetti): until I add hystrix to data_plane_api
  return std::unique_ptr<envoy::config::metrics::v2::HystrixSink>(
      new envoy::config::metrics::v2::HystrixSink());
  // return std::unique_ptr<envoy::config::metrics::v2::StatsdSink>(
  // ica    new envoy::config::metrics::v2::StatsdSink());
}

std::string HystrixSinkFactory::name() { return Config::StatsSinkNames::get().HYSTRIX; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<HystrixSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
