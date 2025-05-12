#include "source/extensions/tracers/xray/config.h"

#include <string>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/trace/v3/xray.pb.h"
#include "envoy/config/trace/v3/xray.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"
#include "source/extensions/tracers/xray/xray_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

XRayTracerFactory::XRayTracerFactory() : FactoryBase("envoy.tracers.xray") {}

Tracing::DriverSharedPtr
XRayTracerFactory::createTracerDriverTyped(const envoy::config::trace::v3::XRayConfig& proto_config,
                                           Server::Configuration::TracerFactoryContext& context) {
  std::string sampling_rules_json;
  TRY_NEEDS_AUDIT {
    sampling_rules_json =
        THROW_OR_RETURN_VALUE(Config::DataSource::read(proto_config.sampling_rule_manifest(), true,
                                                       context.serverFactoryContext().api()),
                              std::string);
  }
  END_TRY catch (EnvoyException& e) {
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

  auto aws = absl::flat_hash_map<std::string, ProtobufWkt::Value>{};
  for (const auto& field : proto_config.segment_fields().aws().fields()) {
    aws.emplace(field.first, field.second);
  }
  const auto& origin = proto_config.segment_fields().origin();
  XRayConfiguration xconfig{endpoint, proto_config.segment_name(), sampling_rules_json, origin,
                            std::move(aws)};

  return std::make_shared<XRay::Driver>(xconfig, context);
}

/**
 * Static registration for the XRay tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(XRayTracerFactory, Server::Configuration::TracerFactory);

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
