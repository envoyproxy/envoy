#include "extensions/tracers/xray/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"
#include "extensions/tracers/xray/xray_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

XRayTracerFactory::XRayTracerFactory() : FactoryBase(TracerNames::get().XRay) {}

Tracing::HttpTracerPtr
XRayTracerFactory::createHttpTracerTyped(const envoy::config::trace::v2::XRayConfig& proto_config,
                                         Server::Instance& server) {
  std::string sampling_rule_json;
  try {
    sampling_rules_json = server.api().fileSystem().fileReadToEnd(proto_config.json_file());
  } catch (EnvoyException& e) {
    ENVOY_LOG(error, "Could not read custom sampling rule json file: {}, because of {}.",
              proto_config.json_file(), e.what());
  }

  XRayConfiguration xconfig(proto_config.daemon_endpoint, proto_config.segment_name,
                            sampling_rules_json);
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
