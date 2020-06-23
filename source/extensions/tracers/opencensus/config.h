#pragma once

#include <string>

#include "envoy/config/trace/v3/opencensus.pb.h"
#include "envoy/config/trace/v3/opencensus.pb.validate.h"

#include "extensions/tracers/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenCensus {

/**
 * Config registration for the OpenCensus tracer. @see TracerFactory.
 */
class OpenCensusTracerFactory
    : public Common::FactoryBase<envoy::config::trace::v3::OpenCensusConfig> {
public:
  OpenCensusTracerFactory();

private:
  // FactoryBase
  Tracing::HttpTracerSharedPtr
  createHttpTracerTyped(const envoy::config::trace::v3::OpenCensusConfig& proto_config,
                        Server::Configuration::TracerFactoryContext& context) override;

  // Since OpenCensus can only support a single tracing configuration per entire process,
  // we need to make sure that it is configured at most once.
  Tracing::HttpTracerSharedPtr tracer_;
  envoy::config::trace::v3::OpenCensusConfig config_;
};

} // namespace OpenCensus
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
