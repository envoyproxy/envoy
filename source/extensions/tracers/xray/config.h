#pragma once

#include "envoy/config/trace/v2/trace.pb.validate.h"

#include "extensions/tracers/common/factory_base.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Config registration for the xray tracer. @see TracerFactory.
                 */
                class XRayTracerFactory : public Common::FactoryBase<envoy::config::trace::v2::XRayConfig> {
                public:
                    XRayTracerFactory();

                private:
                    // FactoryBase
                    Tracing::HttpTracerPtr
                    createHttpTracerTyped(const envoy::config::trace::v2::XRayConfig& proto_config,
                                          Server::Instance& server) override;
                };

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy