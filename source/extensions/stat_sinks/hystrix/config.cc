#include "extensions/stat_sinks/hystrix/config.h"

#include <memory>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/config/metrics/v3/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/network/resolver_impl.h"

#include "extensions/stat_sinks/hystrix/hystrix.h"
#include "extensions/stat_sinks/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Hystrix {

Stats::SinkPtr HystrixSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                   Server::Instance& server) {
  const auto& hystrix_sink =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v3::HystrixSink&>(
          config, server.messageValidationContext().staticValidationVisitor());
  return std::make_unique<Hystrix::HystrixSink>(server, hystrix_sink.num_buckets());
}

ProtobufTypes::MessagePtr HystrixSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::metrics::v3::HystrixSink>();
}

std::string HystrixSinkFactory::name() const { return StatsSinkNames::get().Hystrix; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
REGISTER_FACTORY(HystrixSinkFactory, Server::Configuration::StatsSinkFactory);

} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
