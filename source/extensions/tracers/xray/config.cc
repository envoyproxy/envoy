#include "extensions/tracers/xray/config.h"

#include <string>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/trace/v3/xray.pb.h"
#include "envoy/config/trace/v3/xray.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/config/datasource.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"
#include "extensions/tracers/xray/xray_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

XRayTracerFactory::XRayTracerFactory() : FactoryBase(TracerNames::get().XRay) {}

Tracing::HttpTracerPtr
XRayTracerFactory::createHttpTracerTyped(const envoy::config::trace::v3::XRayConfig& proto_config,
                                         Server::Configuration::TracerFactoryContext& context) {
  std::string sampling_rules_json;
  try {
    sampling_rules_json = Config::DataSource::read(proto_config.sampling_rule_manifest(), true,
                                                   context.serverFactoryContext().api());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Failed to read sampling rules manifest because of {}.", e.what());
  }

  if (proto_config.daemon_endpoint().protocol() != envoy::config::core::v3::SocketAddress::UDP) {
    throw EnvoyException("X-Ray daemon endpoint must be a UDP socket address");
  }

  if (proto_config.daemon_endpoint().port_specifier_case() !=
      envoy::config::core::v3::SocketAddress::PortSpecifierCase::kPortValue) {
    throw EnvoyException("X-Ray daemon port must be specified as number. Not a named port.");
  }

  const std::string endpoint = fmt::format("{}:{}", proto_config.daemon_endpoint().address(),
                                           proto_config.daemon_endpoint().port_value());

  XRayConfiguration xconfig{endpoint, proto_config.segment_name(), sampling_rules_json};
  auto xray_driver = std::make_unique<XRay::Driver>(xconfig, context);

  return std::make_unique<Tracing::HttpTracerImpl>(std::move(xray_driver),
                                                   context.serverFactoryContext().localInfo());
}

/**
 * Static registration for the XRay tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(XRayTracerFactory, Server::Configuration::TracerFactory);

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
