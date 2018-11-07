#pragma once

#include "envoy/server/instance.h"
#include "server/configuration_impl.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Config registration for the xray tracer. @see TracerFactory.
                 */
                // TracerFactory
                class XRayTracerFactory : public Server::Configuration::TracerFactory {
                public:
                    // TracerFactory
                    Tracing::HttpTracerPtr createHttpTracer(const envoy::config::trace::v2::Tracing& configuration,
                                                            Server::Instance& server) override;

                    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
                        return std::make_unique<envoy::config::trace::v2::XRayConfig>();
                    }

                    std::string name() override;
                };

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy