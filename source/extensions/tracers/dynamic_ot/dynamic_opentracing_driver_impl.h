#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

#include "opentracing/dynamic_load.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

/**
 * This driver provides support for dynamically loading tracing libraries into Envoy that provide an
 * implementation of the OpenTracing API (see https://github.com/opentracing/opentracing-cpp).
 * TODO(rnburn): Add an example showing how to use a tracer library with this driver.
 */
class DynamicOpenTracingDriver : public Common::Ot::OpenTracingDriver {
public:
  DynamicOpenTracingDriver(Stats::Store& stats, const std::string& library,
                           const std::string& tracer_config);

  static std::string formatErrorMessage(std::error_code error_code,
                                        const std::string& error_message);

  // Tracer::OpenTracingDriver
  opentracing::Tracer& tracer() override { return *tracer_; }

  PropagationMode propagationMode() const override {
    return OpenTracingDriver::PropagationMode::TracerNative;
  }

private:
  opentracing::DynamicTracingLibraryHandle library_handle_;
  std::shared_ptr<opentracing::Tracer> tracer_;
};

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
