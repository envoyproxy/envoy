#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/tracing/opentracing_driver_impl.h"

#include "opentracing/dynamic_load.h"

namespace Envoy {
namespace Tracing {

/**
 * This driver provides support for dynamically loading tracing libraries into Envoy that provide an
 * implementation of the OpenTracing API (see https://github.com/opentracing/opentracing-cpp).
 */
class DynamicOpenTracingDriver : public OpenTracingDriver {
public:
  DynamicOpenTracingDriver(Stats::Store& stats, const std::string& library,
                           const std::string& tracer_config);

  // Tracer::OpenTracingDriver
  opentracing::Tracer& tracer() override { return *tracer_; }

  PropagationMode propagationMode() const override {
    return OpenTracingDriver::PropagationMode::TracerNative;
  }

private:
  opentracing::DynamicTracingLibraryHandle library_handle_;
  std::shared_ptr<opentracing::Tracer> tracer_;
};

} // namespace Tracing
} // namespace Envoy
