#include "extensions/tracers/xray/config.h"

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"
#include "extensions/tracers/xray/xray_tracer_impl.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                Tracing::HttpTracerPtr XRayTracerFactory::createHttpTracer(const envoy::config::trace::v2::Tracing& configuration, Server::Instance& server){

                    Envoy::Runtime::RandomGenerator& rand = server.random();

                    ProtobufTypes::MessagePtr config_ptr = createEmptyConfigProto();

                    if (configuration.http().has_config()) {
                        MessageUtil::jsonConvert(configuration.http().config(), *config_ptr);
                    }

                    const auto& xray_config =
                            dynamic_cast<const envoy::config::trace::v2::XRayConfig&>(*config_ptr);

                    Tracing::DriverPtr xray_driver{std::make_unique<XRay::Driver>(xray_config, server.clusterManager(),
                                                                        server.stats(), server.threadLocal(),
                                                                        server.runtime(), server.localInfo(), rand)};

                    return Tracing::HttpTracerPtr(new Tracing::HttpTracerImpl(std::move(xray_driver), server.localInfo()));
                }

                std::string XRayTracerFactory::name() { return TracerNames::get().XRay; }

                /**
                 * Static registration for the xray tracer. @see RegisterFactory.
                 */
                static Registry::RegisterFactory<XRayTracerFactory, Server::Configuration::TracerFactory>
                        register_;

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
