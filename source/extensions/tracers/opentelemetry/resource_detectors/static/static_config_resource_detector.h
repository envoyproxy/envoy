#pragma once

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A resource detector that extracts attributes from static config
 *
 */
class StaticConfigResourceDetector : public ResourceDetector,
                                     Logger::Loggable<Logger::Id::tracing> {
public:
  StaticConfigResourceDetector(const envoy::extensions::tracers::opentelemetry::resource_detectors::
                                   v3::StaticConfigResourceDetectorConfig& config,
                               Server::Configuration::ServerFactoryContext&);
  ResourceConstSharedPtr detect() const override { return resource_; }
  ResourceConstSharedPtr detect(const StreamInfo::StreamInfo&) const override { return nullptr; }

private:
  ResourceConstSharedPtr resource_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
