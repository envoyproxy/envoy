#include "cxx_swift_interop.h"

#include "source/server/options_impl_base.h"

#include "library/common/engine.h"

namespace Envoy {
namespace CxxSwift {

void run(BootstrapPtr bootstrap_ptr, Platform::LogLevel log_level, envoy_engine_t engine_handle) {
  auto options = std::make_unique<Envoy::OptionsImplBase>();
  options->setConfigProto(bootstrapFromPtr(bootstrap_ptr));
  options->setLogLevel(static_cast<spdlog::level::level_enum>(log_level));
  options->setConcurrency(1);
  reinterpret_cast<Envoy::Engine*>(engine_handle)->run(std::move(options));
}

} // namespace CxxSwift
} // namespace Envoy
