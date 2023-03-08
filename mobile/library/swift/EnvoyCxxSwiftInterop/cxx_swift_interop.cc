#import "cxx_swift_interop.h"
#import "source/server/options_impl.h"
#import "library/common/engine.h"

namespace Envoy {
namespace CxxSwift {

void run(BootstrapPtr bootstrap_ptr, std::string log_level, envoy_engine_t engine_handle) {
  auto options = std::make_unique<Envoy::OptionsImpl>();
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));
  options->setConfigProto(std::move(bootstrap));
  options->setLogLevel(options->parseAndValidateLogLevel(log_level));
  options->setConcurrency(1);
  reinterpret_cast<Envoy::Engine*>(engine_handle)->run(std::move(options));
}

} // namespace CxxSwift
} // namespace Envoy
