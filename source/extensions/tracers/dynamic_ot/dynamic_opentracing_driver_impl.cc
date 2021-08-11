#include "source/extensions/tracers/dynamic_ot/dynamic_opentracing_driver_impl.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

DynamicOpenTracingDriver::DynamicOpenTracingDriver(Stats::Scope& scope, const std::string& library,
                                                   const std::string& tracer_config)
    : OpenTracingDriver{scope} {
  std::string error_message;
  opentracing::expected<opentracing::DynamicTracingLibraryHandle> library_handle_maybe =
      opentracing::DynamicallyLoadTracingLibrary(library.c_str(), error_message);
  if (!library_handle_maybe) {
    throw EnvoyException{formatErrorMessage(library_handle_maybe.error(), error_message)};
  }
  library_handle_ = std::move(*library_handle_maybe);

  opentracing::expected<std::shared_ptr<opentracing::Tracer>> tracer_maybe =
      library_handle_.tracer_factory().MakeTracer(tracer_config.c_str(), error_message);
  if (!tracer_maybe) {
    throw EnvoyException{formatErrorMessage(tracer_maybe.error(), error_message)};
  }
  tracer_ = std::move(*tracer_maybe);
  RELEASE_ASSERT(tracer_ != nullptr, "");
}

std::string DynamicOpenTracingDriver::formatErrorMessage(std::error_code error_code,
                                                         const std::string& error_message) {
  if (error_message.empty()) {
    return absl::StrCat("", error_code.message());
  } else {
    return fmt::format("{}: {}", error_code.message(), error_message);
  }
}

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
