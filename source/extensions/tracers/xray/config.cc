#include "extensions/tracers/xray/config.h"

#include <string>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/config/trace/v2alpha/xray.pb.h"
#include "envoy/config/trace/v2alpha/xray.pb.validate.h"
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

Tracing::HttpTracerPtr XRayTracerFactory::createHttpTracerTyped(
    const envoy::config::trace::v2alpha::XRayConfig& proto_config, Server::Instance& server) {
  std::string sampling_rules_json;
  try {
    sampling_rules_json =
        Config::DataSource::read(proto_config.sampling_rule_manifest(), true, server.api());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Failed to read sampling rules manifest because of {}.", e.what());
  }

  if (proto_config.daemon_endpoint().protocol() != envoy::api::v2::core::SocketAddress::UDP) {
    throw EnvoyException("X-Ray daemon endpoint must be a UDP socket address");
  }

  if (proto_config.daemon_endpoint().port_specifier_case() !=
      envoy::api::v2::core::SocketAddress::PortSpecifierCase::kPortValue) {
    throw EnvoyException("X-Ray daemon port must be specified as number. Not a named port.");
  }

  const std::string endpoint = fmt::format("{}:{}", proto_config.daemon_endpoint().address(),
                                           proto_config.daemon_endpoint().port_value());

  XRayConfiguration xconfig{endpoint, proto_config.segment_name(), sampling_rules_json};
  auto xray_driver = std::make_unique<XRay::Driver>(xconfig, server);

  return std::make_unique<Tracing::HttpTracerImpl>(std::move(xray_driver), server.localInfo());
}

/**
 * Static registration for the XRay tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(XRayTracerFactory, Server::Configuration::TracerFactory);

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
