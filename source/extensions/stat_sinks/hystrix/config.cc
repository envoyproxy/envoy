#include "extensions/stat_sinks/hystrix/config.h"

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/config/metrics/v2/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/network/resolver_impl.h"

#include "extensions/stat_sinks/common/hystrix/hystrix.h"
#include "extensions/stat_sinks/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace HystrixNameSpace {

Stats::SinkPtr HystrixSinkFactory::createStatsSink(const Protobuf::Message&,
                                                   Server::Instance& server) {
  return std::make_unique<Common::HystrixNameSpace::HystrixSink>(server);
}

ProtobufTypes::MessagePtr HystrixSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::config::metrics::v2::HystrixSink>(
      new envoy::config::metrics::v2::HystrixSink());
}

std::string HystrixSinkFactory::name() { return StatsSinkNames::get().HYSTRIX; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<HystrixSinkFactory, Server::Configuration::StatsSinkFactory>
    register_;

} // namespace HystrixNameSpace
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
